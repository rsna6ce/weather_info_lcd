#include "esp_system.h"
#include <LCD_I2C.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>
#include <WiFiClientSecure.h>

#define JST 3600*9
#define LED_PIN 2
#define SW_PIN 15

// weather infomation site setting
//#define USE_TENKI_JP
#define USE_WEATHERNEWS

// for debug
//#define DEBUG_WITHOUT_PERIPHERAL_DEVICE

#define USE_TOUCHPANEL_SW
#if defined(USE_TOUCHPANEL_SW)
# define SW_PIN_MODE INPUT
# define SW_ON       HIGH
#else
# define SW_PIN_MODE INPUT_PULLUP
# define SW_ON       LOW
#endif


#if defined(USE_WEATHERNEWS)
LCD_I2C lcd(0x27, 20, 4);
#else
LCD_I2C lcd(0x27, 16, 2);
#endif

WiFiClientSecure client;

// Wifi definition (Multiple SSIDs can be set)
const char* ssid_list[] = {
    "TpLink-xxxx", 
};
const char* password_list[] = {
    "hogehoge",
};
const int wifi_try_count_sec = 10;

// place definition
const String place_name = "ﾌﾅﾊﾞｼｼ"; // change your area name for display.

//OpenWeatherMAP definition
const String endpoint = "https://api.openweathermap.org/data/2.5/forecast?units=metric";
const String zip_code = "274-0000,JP"; // -> change your zip code
const String key = "xxxxxxxx"; // -> input your OpenWeatherMAP key

//tenki.jp definition
const String tenki_jp_url = "https://tenki.jp/forecast/3/15/4510/12204/3hours.html"; // -> change your area. find in tenki.jp site.
const char* tenki_jp_host = "tenki.jp";

//weathernews definition
const String weathernews_url = "https://weathernews.jp/onebox/tenki/chiba/12204/"; // -> change your area. find in weathernews site.
const char* weathernews_host = "weathernews.jp";

// 3h forcast weather info definition
struct weather_info {
  int hour;
  int temperature;
  int wind_speed;
  double rain_1h_mm;
  int prob_precip;
  int icon;
};
#if defined (USE_TENKI_JP)
const int weather_info_count = 5;
const int weather_item_count = 6;
#elif defined (USE_WEATHERNEWS)
const int weather_info_count = 12;
const int weather_item_count = 5;
#else
const int weather_info_count = 5;
const int weather_item_count = 5;
#endif
weather_info weather_info_list[weather_info_count] = {0};

// display mode definition
enum display_mode{
    mode_date=0,
    mode_weather,
    mode_temp,
    mode_wind,
    mode_rain,
#if defined (USE_TENKI_JP)
    mode_prob,
#endif
    mode_end};
int curr_display_mode = mode_date;

// timer 
const int interval_wether_update = 90;
#if defined(USE_WEATHERNEWS)
const int interval_display_mode = 8;
#else
const int interval_display_mode = 5;
#endif
const int interval_backlight_off = 180;
int timer_wether_update = 0;
int timer_display_mode = 0;
int timer_backlight_off = 0;

//watchdog timer
#define WDT_TIMEOUT_MS 30000
hw_timer_t *wdtimer = NULL;
void IRAM_ATTR resetModule() {
  ets_printf("Reboot\n");
  esp_restart();
}

// ConvStr function from(https://synapse.kyoto/)
String ConvStr(String str)
{
  struct LocalFunc{ // for defining local function
    static uint8_t CodeUTF8(uint8_t ch)
    {
      static uint8_t OneNum=0; // Number of successive 1s at MSBs first byte (Number of remaining bytes)
      static uint16_t Utf16; // UTF-16 code for multi byte character
      static boolean InUtf16Area; // Flag that shows character can be expressed as UTF-16 code

      if(OneNum==0) { // First byte
        uint8_t c;

        // Get OneNum
        c=ch;
        while(c&0x80) {
          c<<=1;
          OneNum++;
        } // while

        if(OneNum==1 || OneNum>6) { // First byte is in undefined area
          OneNum=0;
          return ch;
        } else if(OneNum==0) { // 1-byte character
          return ch;
        } else { // Multi byte character
          InUtf16Area=true;
          Utf16=ch&((1<<(7-OneNum--))-1); // Get first byte
        } // if
      } else { // not first byte
        if((ch&0xc0)!=0x80) { // First byte appears illegally
          OneNum=0;
          return ch;
        } // if
        if(Utf16&0xfc00) InUtf16Area=false;
        Utf16=(Utf16<<6)+(ch&0x3f);
        if(--OneNum==0) { // Last byte
          return (InUtf16Area && Utf16>=0xff61 && Utf16<=0xff9f) ? Utf16-(0xff61-0xa1) // kana
                                                                 : ' ';                // other character
        } // if
      } // if

      return 0;
    }; // CodeUTF8
  }; // LocalFunc

  const char charA[]="ｱ";
  if(*charA=='\xb1') return str;
  String result="";
  for(int i=0; i<str.length(); i++) {
    uint8_t b=LocalFunc::CodeUTF8((uint8_t)str.c_str()[i]);
    if(b) {
      result+=(char)b;
    } // if
  } // for i
  return result;
} // ConvStr

void setup() {
    pinMode(LED_PIN, OUTPUT);
    pinMode(SW_PIN, SW_PIN_MODE);
    lcd.begin();
    lcd.clear();
    lcd.backlight();
    timer_backlight_off = interval_backlight_off;
    lcd.setCursor(0, 0);
    lcd.print(ConvStr("ｼﾞｭﾝﾋﾞﾁｭｳ...  "));
    lcd.setCursor(0, 1);
    lcd.print(ConvStr("ｼﾊﾞﾗｸｵﾏﾁｸﾀﾞｻｲ"));

    Serial.begin(115200);

    int led_blink = 0;
    int ssid_count = sizeof(ssid_list) / sizeof(ssid_list[0]);
    bool connected = false;
    for (int i =0; i<ssid_count; i++) {
        Serial.println("Connecting to WiFi..");
        Serial.println(ssid_list[i]);
        WiFi.begin(ssid_list[i], password_list[i]);
        for (int j=0; j<wifi_try_count_sec*4; j++) {
            digitalWrite(LED_BUILTIN, led_blink); led_blink ^= 1;
            delay(250);
            if (WiFi.status() == WL_CONNECTED) {
                connected = true;
                digitalWrite(LED_BUILTIN, LOW);
                Serial.println(" OK");
                break;
            }
            Serial.print(".");
        }
        if (connected) {
            Serial.println("Connected to the WiFi network");
            break;
        } else {
            digitalWrite(LED_BUILTIN, HIGH);
            WiFi.disconnect();
            delay(100);
        }
    }

    delay(1*1000);
    configTime( JST, 0, "ntp.nict.jp", "ntp.jst.mfeed.ad.jp");
    delay(1*1000);
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        // may be wifh weak reset
        Serial.println("reboot ...");
        esp_restart();
    } else {
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    }

    lcd.setCursor(0, 0);
    lcd.print(ConvStr("ﾃﾝｷﾖﾎｳ ﾃﾞｰﾀﾃｲｷｮｳ"));
    lcd.setCursor(0, 1);
#if defined(USE_TENKI_JP)
    lcd.print(ConvStr("tenki.jp        "));
#elif defined(USE_WEATHERNEWS)
    lcd.print(ConvStr("Weathernews Inc."));
#else
    lcd.print(ConvStr("OpenWeatherMAP  "));
#endif

    //skip verification
    client.setInsecure();

    //watchdog timer
    wdtimer = timerBegin(0, 80, true);
    timerAttachInterrupt(wdtimer, &resetModule, true);
    timerAlarmWrite(wdtimer, WDT_TIMEOUT_MS * 1000, false);
    timerAlarmEnable(wdtimer);

    delay(5*1000);
}

bool update_weather_3h_owm() {
    String api_url = endpoint;
    api_url += ("&zip=" + zip_code);
    api_url += ("&cnt=" + String(weather_info_count));
    api_url += ("&APPID=" + key);
    
    HTTPClient http;
    http.begin(api_url);
    int httpCode = http.GET();
    if (httpCode > 0) {
        String payload = http.getString();
        Serial.println(httpCode);
        Serial.println(payload);
        http.end();

        DynamicJsonBuffer jsonBuffer;
        String json = payload;
        JsonObject& weatherdata = jsonBuffer.parseObject(json);
        if(!weatherdata.success()){
            Serial.println("parseObject() failed");
            return false;
        } else {
            for (int i=0; i<weather_info_count; i++) {
                Serial.println("---------");
                Serial.println(weatherdata["list"][i]["dt_txt"].as<String>());
                Serial.println(weatherdata["list"][i]["dt_txt"].as<String>().substring(11,13));
                int utc_hour = weatherdata["list"][i]["dt_txt"].as<String>().substring(11,13).toInt();
                int jst_hour = (utc_hour + 9) % 24;
                Serial.println(jst_hour);
                weather_info_list[i].hour = jst_hour;

                Serial.println(weatherdata["list"][i]["weather"][0]["icon"].as<String>());
                int icon = weatherdata["list"][i]["weather"][0]["icon"].as<String>().substring(0,2).toInt();
                Serial.println(icon);
                weather_info_list[i].icon = icon;

                Serial.println(weatherdata["list"][i]["main"]["temp"].as<double>());
                double temp_f = weatherdata["list"][i]["main"]["temp"].as<double>();
                int temp = temp_f < 0 ? int(temp_f-0.5) : int(temp_f+0.5);
                Serial.println(temp);
                weather_info_list[i].temperature = temp;

                Serial.println(weatherdata["list"][i]["wind"]["speed"].as<double>());
                int wind = int(weatherdata["list"][i]["wind"]["speed"].as<double>() + 0.5);
                Serial.println(wind);
                weather_info_list[i].wind_speed = wind;
                
                Serial.println(weatherdata["list"][i]["rain"]["3h"].as<double>());
                double rain = weatherdata["list"][i]["rain"]["3h"].as<double>();
                Serial.println(rain);
                weather_info_list[i].rain_1h_mm = rain/3.0;
            }
        }
    } else {
        Serial.println("http get weather failed");
        http.end();
        return false;
    }
}

bool update_weather_3h_tenki_jp() {
    int led_blink = 0;
    digitalWrite(LED_BUILTIN, led_blink); led_blink ^= 1;
    if (!client.connect(tenki_jp_host, 443)) {
        Serial.println("Connection failed!");
        return false;
    }
    Serial.println("Connected to server!");

    // Make a HTTP request:
    client.println("GET " + tenki_jp_url + " HTTP/1.0");
    client.println("Host: " + String(tenki_jp_host));
    client.println("User-Agent: Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/51.0.2704.103 Safari/537.36");
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        digitalWrite(LED_BUILTIN, led_blink); led_blink ^= 1;
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            Serial.println("headers received");
            break;
        }
    }

    //parse HTML
    bool return_status = false;
    bool break_flag = false;
    bool table_found = false;
    int table_index_weather = 0;
    int table_index_hour = 0;
    int table_index_temperature = 0;
    int table_index_wind_speed = 0;
    int table_index_precipitation = 0;
    int table_index_prob_precip = 0;
    int found_item_count = 0;
    String curr_block_name = "";
    while (client.connected() && !break_flag) {
        while (client.available()) {
            digitalWrite(LED_BUILTIN, led_blink); led_blink ^= 1;
            String line = client.readStringUntil('\n');
            if (line == "</html>") {
                //Serial.println("end of html.break1!!");
                break_flag = true;
                break;
            } else if (table_found) {
                //Serial.println(line);
                if (line.indexOf("</table>", 0) >= 0) {
                    //Serial.println("end of table!!");
                    table_found = false;
                    if (found_item_count == weather_info_count*weather_item_count) {
                        Serial.println("found item count OK!!");
                        return_status = true;
                        break_flag = true;
                        break;
                    }
                } else if (line.indexOf("<tr class=\"weather\">") >= 0) {
                    curr_block_name = "weather";
                } else if (curr_block_name=="weather" && table_index_weather<weather_info_count && line.indexOf("<p>") >= 0) {
                    found_item_count++;
                    int tag_len = String("<p>").length();
                    int tag_idx = line.indexOf("<p>");
                    int tag_idx2 = line.lastIndexOf("</p>");
                    String weather_str = line.substring(tag_idx+tag_len, tag_idx2);
                    // fit to openweathermap icon value
                    if (weather_str.indexOf("晴") >=0) {
                        //case  1: weather_jp[i] = "ﾊﾚ"; break;
                        weather_info_list[table_index_weather].icon = 1;
                    } else if (weather_str.indexOf("曇") >=0) {
                        //case  3: weather_jp[i] = "ｸﾓ"; break;
                        weather_info_list[table_index_weather].icon = 3;
                    } else if (weather_str.indexOf("雨") >=0) {
                        //case  9: weather_jp[i] = "ｱﾒ"; break;
                        weather_info_list[table_index_weather].icon = 9;
                    } else if (weather_str.indexOf("雷") >=0) {
                        //case 11: weather_jp[i] = "ﾗｲ"; break;
                        weather_info_list[table_index_weather].icon = 11;
                    } else if (weather_str.indexOf("雪") >=0) {
                        //case 13: weather_jp[i] = "ﾕｷ"; break;
                        weather_info_list[table_index_weather].icon = 13;
                    } else if (weather_str.indexOf("ひょう") >=0) {
                        //case 50: weather_jp[i] = "ﾕｷ"; break;
                        weather_info_list[table_index_weather].icon = 13;
                    } else if (weather_str.indexOf("みぞれ") >=0) {
                        //case 50: weather_jp[i] = "ﾕｷ"; break;
                        weather_info_list[table_index_weather].icon = 13;
                    } else if (weather_str.indexOf("あられ") >=0) {
                        //case 50: weather_jp[i] = "ﾕｷ"; break;
                        weather_info_list[table_index_weather].icon = 13;
                    } else if (weather_str.indexOf("砂") >=0) {
                        //case 50: weather_jp[i] = "ｽﾅ"; break;
                        weather_info_list[table_index_weather].icon = 90;
                    } else if (weather_str.indexOf("煙") >=0) {
                        //case 50: weather_jp[i] = "ｽﾅ"; break;
                        weather_info_list[table_index_weather].icon = 90;
                    } else if (weather_str.indexOf("霧") >=0) {
                        //case 50: weather_jp[i] = "ｷﾘ"; break;
                        weather_info_list[table_index_weather].icon = 50;
                    } else {
                        //default: weather_jp[i] = "??"; break;
                        weather_info_list[table_index_weather].icon = 99;
                    }
                    table_index_weather++;

                } else if (line.indexOf("<tr class=\"hour\">") >= 0) {
                    curr_block_name = "hour";
                } else if (curr_block_name=="hour" && table_index_hour<weather_info_count && line.indexOf("<td><span>") >= 0) {
                    found_item_count++;
                    int tag_len = String("<td><span>").length();
                    int tag_idx = line.indexOf("<td><span>");
                    int tag_idx2 = line.lastIndexOf("</span></td>");
                    String hour_str = line.substring(tag_idx+tag_len, tag_idx2);
                    weather_info_list[table_index_hour].hour = hour_str.toInt();
                    table_index_hour++;

                } else if (line.indexOf("<tr class=\"temperature\">") >= 0) {
                    curr_block_name = "temperature";
                } else if (curr_block_name=="temperature" && table_index_temperature < weather_info_count && line.indexOf("<td><span>") >= 0) {
                    found_item_count++;
                    int tag_len = String("<td><span>").length();
                    int tag_idx = line.indexOf("<td><span>");
                    int tag_idx2 = line.lastIndexOf("</span></td>");
                    String temp_str = line.substring(tag_idx+tag_len, tag_idx2);
                    double temp_f = temp_str.toDouble();
                    int temp = temp_f < 0 ? int(temp_f-0.5) : int(temp_f+0.5);
                    weather_info_list[table_index_temperature].temperature = temp;
                    table_index_temperature++;

                } else if (line.indexOf("<tr class=\"wind-speed\">") >= 0) {
                    curr_block_name = "wind-speed";
                } else if (curr_block_name=="wind-speed" && table_index_wind_speed<weather_info_count && line.indexOf("<td><span>") >= 0) {
                    found_item_count++;
                    int tag_len = String("<td><span>").length();
                    int tag_idx = line.indexOf("<td><span>");
                    int tag_idx2 = line.lastIndexOf("</span></td>");
                    String wind_str = line.substring(tag_idx+tag_len, tag_idx2);
                    weather_info_list[table_index_wind_speed].wind_speed = wind_str.toInt();
                    table_index_wind_speed++;

                } else if (line.indexOf("<tr class=\"precipitation\">") >= 0) {
                    curr_block_name = "precipitation";
                } else if (curr_block_name=="precipitation" && table_index_precipitation<weather_info_count && line.indexOf("<td><span>") >= 0) {
                    found_item_count++;
                    int tag_len = String("<td><span>").length();
                    int tag_idx = line.indexOf("<td><span>");
                    int tag_idx2 = line.lastIndexOf("</span></td>");
                    String rain_str = line.substring(tag_idx+tag_len, tag_idx2);
                    weather_info_list[table_index_precipitation].rain_1h_mm = rain_str.toDouble();
                    table_index_precipitation++;

                } else if (line.indexOf("<tr class=\"prob-precip\">") >= 0) {
                    curr_block_name = "prob-precip";
                } else if (curr_block_name=="prob-precip" && table_index_prob_precip<weather_info_count && line.indexOf("<td><span>") >= 0) {
                    found_item_count++;
                    int tag_len = String("<td><span>").length();
                    int tag_idx = line.indexOf("<td><span>");
                    int tag_idx2 = line.lastIndexOf("</span></td>");
                    String prob_str = line.substring(tag_idx+tag_len, tag_idx2);
                    weather_info_list[table_index_prob_precip].prob_precip = min(int(prob_str.toInt()),99); //99=display 2digits limit
                    table_index_prob_precip++;

                } else if (line.indexOf("</tr>") >=0) {
                    curr_block_name = "";
                }
            } else if (line.indexOf("<table id=\"forecast-point-3h-to", 0) >= 0) {
                table_found = true;
                //Serial.println("table found!");
            }
        }
    }
    client.stop();
    return return_status;
}

bool update_weather_1h_weathernews() {
    Serial.println("update_weather_1h_weathernews");
    int led_blink = 0;
    digitalWrite(LED_BUILTIN, led_blink); led_blink ^= 1;
    if (!client.connect(weathernews_host, 443)) {
        Serial.println("Connection failed!");
        return false;
    }
    Serial.println("Connected to server!");

    // Make a HTTP request:
    client.println("GET " + weathernews_url + " HTTP/1.0");
    client.println("Host: " + String(weathernews_host));
    client.println("Connection: close");
    client.println();

    while (client.connected()) {
        digitalWrite(LED_BUILTIN, led_blink); led_blink ^= 1;
        String line = client.readStringUntil('\n');
        if (line == "\r") {
            Serial.println("headers received");
            break;
        }
    }

    //parse HTML
    bool return_status = false;
    bool break_flag = false;
    bool table_found = false;
    int table_index_weather = 0;
    int table_index_hour = 0;
    int table_index_temperature = 0;
    int table_index_wind_speed = 0;
    int table_index_precipitation = 0;
    int table_index_prob_precip = 0;
    int found_item_count = 0;
    String curr_block_name = "";
    while (client.connected() && !break_flag) {
        while (client.available()) {
            digitalWrite(LED_BUILTIN, led_blink); led_blink ^= 1;
            String line = client.readStringUntil('\n');
            if (line == "</html>") {
                //Serial.println("end of html.break1!!");
                break_flag = true;
                break;
            } else if (table_found) {
                //Serial.println(line);
                if (line.indexOf("</section>", 0) >= 0 && table_index_weather >= weather_info_count) {
                    //Serial.println("end of table!!");
                    table_found = false;
                    if (found_item_count == weather_info_count*weather_item_count) {
                        Serial.println("found item count OK!!");
                        return_status = true;
                        break_flag = true;
                        break;
                    }

                // weather
                } else if (line.indexOf("<li class=\"weather\">") >= 0) {
                    curr_block_name = "weather";
                } else if (curr_block_name=="weather" && table_index_weather<weather_info_count && line.indexOf("wxicon/")>=0) {
                    found_item_count++;
                    int tag_len = String("wxicon/").length();
                    int tag_idx = line.indexOf("wxicon/");
                    int tag_idx2 = line.lastIndexOf(".png");
                    String weather_str = line.substring(tag_idx+tag_len, tag_idx2);
                    int weather_num = weather_str.toInt();
                    // fit to openweathermap icon value
                    switch(weather_num) {
                        case 100:
                        case 123:
                        case 124:
                        case 130:
                        case 131:
                        case 500:
                        case 550:
                        case 600:
                            //case  1: weather_jp[i] = "ﾊﾚ"; break;
                            weather_info_list[table_index_weather].icon = 1;
                            break;
                        case 200:
                        case 209:
                        case 231:
                            //case  3: weather_jp[i] = "ｸﾓ"; break;
                            weather_info_list[table_index_weather].icon = 3;
                            break;
                        case 300:
                        case 304:
                        case 306:
                        case 308:
                        case 328:
                        case 329:
                        case 350:
                        case 650:
                        case 850:
                            //case  9: weather_jp[i] = "ｱﾒ"; break;
                            weather_info_list[table_index_weather].icon = 9;
                            break;
                        case 800:
                            //case 11: weather_jp[i] = "ﾗｲ"; break;
                            weather_info_list[table_index_weather].icon = 11;
                            break;
                        case 340:
                        case 400:
                        case 405:
                        case 406:
                        case 407:
                        case 425:
                        case 426:
                        case 427:
                        case 430:
                        case 450:
                        case 950:
                            //case 13: weather_jp[i] = "ﾕｷ"; break;
                            weather_info_list[table_index_weather].icon = 13;
                            break;
                        default:
                            //default: weather_jp[i] = "??"; break;
                            weather_info_list[table_index_weather].icon = 99;
                    }
                    table_index_weather++;
                    curr_block_name = "";

                // hour
                } else if (line.indexOf("<li class=\"time\">") >= 0){
                    curr_block_name = "hour";
                } else if (curr_block_name=="hour" && table_index_hour<weather_info_count) {
                    found_item_count++;
                    int tag_len = String("<p>").length();
                    int tag_idx = line.indexOf("<p>");
                    int tag_idx2 = line.lastIndexOf("</p>");
                    String hour_str = line.substring(tag_idx+tag_len, tag_idx2);
                    weather_info_list[table_index_hour].hour = hour_str.toInt();
                    table_index_hour++;
                    curr_block_name = "";

                // temperature
                } else if (line.indexOf("<li class=\"temp\">") >= 0){
                    curr_block_name = "temp";
                } else if (curr_block_name=="temp" && table_index_temperature < weather_info_count) {
                    found_item_count++;
                    int tag_len = String("<p>").length();
                    int tag_idx = line.indexOf("<p>");
                    int tag_idx2 = line.lastIndexOf("<span");
                    String temp_str = line.substring(tag_idx+tag_len, tag_idx2);
                    double temp_f = temp_str.toDouble();
                    int temp = temp_f < 0 ? int(temp_f-0.5) : int(temp_f+0.5);
                    weather_info_list[table_index_temperature].temperature = temp;
                    table_index_temperature++;
                    curr_block_name = "";

                // wind-speed
                } else if (line.indexOf("<li class=\"wind\">") >= 0) {
                    curr_block_name = "wind-speed";
                } else if (curr_block_name == "wind-speed" && table_index_wind_speed<weather_info_count && line.indexOf("</span></p>") >=0) {
                    found_item_count++;
                    int tag_len = String("<p>").length();
                    int tag_idx = line.indexOf("<p>");
                    int tag_idx2 = line.lastIndexOf("<span");
                    String wind_str = line.substring(tag_idx+tag_len, tag_idx2);
                    weather_info_list[table_index_wind_speed].wind_speed = wind_str.toInt();
                    table_index_wind_speed++;
                    curr_block_name = "";

                // rain precipitation
                } else if (line.indexOf("<li class=\"rain\">") >= 0) {
                    curr_block_name = "rain-precipitation";
                } else if (curr_block_name=="rain-precipitation" && table_index_precipitation<weather_info_count) {
                    found_item_count++;
                    int tag_len = String("<p>").length();
                    int tag_idx = line.indexOf("<p>");
                    //int tag_idx2 = line.lastIndexOf("<span");
                    //String rain_str = line.substring(tag_idx+tag_len, tag_idx2);
                    String rain_str = line.substring(tag_idx+tag_len);
                    weather_info_list[table_index_precipitation].rain_1h_mm = rain_str.toDouble();
                    table_index_precipitation++;
                    curr_block_name="";

                } else if (line.indexOf("</tr>") >=0) {
                    curr_block_name = "";
                }
            } else if (line.indexOf("id=\"flick_list_1hour\"", 0) >= 0) {
                table_found = true;
                Serial.println("table found!");
            }
        }
    }
    client.stop();
    return return_status;
}

void display_weather_info_3h(int disp_mode) {
    char line1[64] = {0};
    char line2[64] = {0};
    if (disp_mode == mode_date) {
        time_t t;
        struct tm *tm;
        static const char *wd_en[7] = {"Sun","Mon","Tue","Wed","Thr","Fri","Sat"};
        static const char *wd_jp[7] = {"ﾆﾁﾖｳﾋﾞ", "ｹﾞﾂﾖｳﾋﾞ", "ｶﾖｳﾋﾞ", "ｽｲﾖｳﾋﾞ", "ﾓｸﾖｳﾋﾞ", "ｷﾝﾖｳﾋﾞ", "ﾄﾞﾖｳﾋﾞ"};
        t = time(NULL);
        tm = localtime(&t);
        sprintf(line1, "%04d/%02d/%02d %02d:%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min);
        sprintf(line2, "(%s) %s      ", wd_en[tm->tm_wday], wd_jp[tm->tm_wday]);
    } else {
        String line1_first = "";
        String line2_first = "";
        if (disp_mode == mode_weather) {
            line1_first = "ﾃ ";
            line2_first = "ﾝ ";
            char *weather_jp[weather_info_count] = {0};
            for (int i=0; i<weather_info_count; i++) {
                switch (weather_info_list[i].icon) {
                    case  1: weather_jp[i] = "ﾊﾚ"; break;
                    case  2: weather_jp[i] = "ﾊﾚ"; break;
                    case  3: weather_jp[i] = "ｸﾓ"; break;
                    case  4: weather_jp[i] = "ｸﾓ"; break;
                    case  9: weather_jp[i] = "ｱﾒ"; break;
                    case 10: weather_jp[i] = "ｱﾒ"; break;
                    case 11: weather_jp[i] = "ﾗｲ"; break;
                    case 13: weather_jp[i] = "ﾕｷ"; break;
                    case 50: weather_jp[i] = "ｷﾘ"; break;
                    case 90: weather_jp[i] = "ｽﾅ"; break;
                    default: weather_jp[i] = "??"; break;
                }
            }
            sprintf(line2, "%s%s %s %s %s %s",
                line2_first,
                weather_jp[0],
                weather_jp[1],
                weather_jp[2],
                weather_jp[3],
                weather_jp[4]);
        } else if (disp_mode == mode_temp) {
            line1_first = "ｵ ";
            line2_first = "ﾝ ";
            sprintf(line2, "%s%2d %2d %2d %2d %2d",
                line2_first,
                weather_info_list[0].temperature,
                weather_info_list[1].temperature,
                weather_info_list[2].temperature,
                weather_info_list[3].temperature,
                weather_info_list[4].temperature);
        } else if (disp_mode == mode_wind) {
            line1_first = "ｶ ";
            line2_first = "ｾﾞ";
            sprintf(line2, "%s%2d %2d %2d %2d %2d",
                line2_first,
                weather_info_list[0].wind_speed,
                weather_info_list[1].wind_speed,
                weather_info_list[2].wind_speed,
                weather_info_list[3].wind_speed,
                weather_info_list[4].wind_speed);
        } else if (disp_mode == mode_rain) {
            line1_first = "ｱ ";
            line2_first = "ﾒ ";
            int rain_level[weather_info_count] = {0};
            for (int i=0; i<weather_info_count; i++) {
                double rain_1h_mm = weather_info_list[i].rain_1h_mm;
#if defined(USE_TENKI_JP)
                // Add level correction for rain forecast to round down the decimal point. (specifications of tenkijp)
                if (weather_info_list[i].icon==9 or weather_info_list[i].icon==10) {
                    rain_1h_mm += 0.1;
                }
#endif
                if (rain_1h_mm < 0.1) {
                    rain_level[i] = 0;
                } else if (0.1 <= rain_1h_mm && rain_1h_mm < 0.5) {
                    rain_level[i] = 1;
                } else if (0.5 <= rain_1h_mm && rain_1h_mm < 1.0) {
                    rain_level[i] = 2;
                } else if (1.0 <= rain_1h_mm && rain_1h_mm < 4.0) {
                    rain_level[i] = 3;
                } else if (4.0 <= rain_1h_mm && rain_1h_mm < 7.5) {
                    rain_level[i] = 3;
                } else /*7.5<=rain_1h_mm*/ {
                    rain_level[i] = 5;
                }
            }
            sprintf(line2, "%sL%d L%d L%d L%d L%d",
                line2_first,
                rain_level[0],
                rain_level[1],
                rain_level[2],
                rain_level[3],
                rain_level[4]);
#if defined (USE_TENKI_JP)
        } else if (disp_mode == mode_prob) {
            line1_first = "ｶ ";
            line2_first = "ｸ ";
            sprintf(line2, "%s%2d %2d %2d %2d %2d",
                line2_first,
                weather_info_list[0].prob_precip,
                weather_info_list[1].prob_precip,
                weather_info_list[2].prob_precip,
                weather_info_list[3].prob_precip,
                weather_info_list[4].prob_precip);
#endif
        }
        sprintf(line1, "%s%02d-%02d-%02d-%02d-%02d",
            line1_first,
            weather_info_list[0].hour,
            weather_info_list[1].hour,
            weather_info_list[2].hour,
            weather_info_list[3].hour,
            weather_info_list[4].hour);
    }
    lcd.setCursor(0, 0);
    lcd.print(ConvStr(line1));
    lcd.setCursor(0, 1);
    lcd.print(ConvStr(line2));
}

void display_weather_info_1h(int disp_mode) {
    char line1[64] = {0};
    char line2[64] = {0};
    char line3[64] = {0};
    char line4[64] = {0};
    if (disp_mode == mode_date) {
        time_t t;
        struct tm *tm;
        static const char *wd_en[7] = {"Sunday","Monday","Tuesday","Wednesday","Thrusday","Friday","Saturday"};
        static const char *wd_jp[7] = {"ﾆﾁﾖｳﾋﾞ", "ｹﾞﾂﾖｳﾋﾞ", "ｶﾖｳﾋﾞ", "ｽｲﾖｳﾋﾞ", "ﾓｸﾖｳﾋﾞ", "ｷﾝﾖｳﾋﾞ", "ﾄﾞﾖｳﾋﾞ"};
        t = time(NULL);
        tm = localtime(&t);
        sprintf(line1, "%04d/%02d/%02d %02d:%02d", tm->tm_year+1900, tm->tm_mon+1, tm->tm_mday, tm->tm_hour, tm->tm_min);
        sprintf(line2, "(%s) %s", wd_en[tm->tm_wday], wd_jp[tm->tm_wday]);
    } else {
        String line1_first = "";
        String line2_first = "";
        String line3_first = "";
        String line4_first = "";
        if (disp_mode == mode_weather) {
            line1_first = "ﾃ ";
            line2_first = "ﾝ ";
            line3_first = "ｷ ";
            line4_first = "  ";
            char *weather_jp[weather_info_count] = {0};
            for (int i=0; i<weather_info_count; i++) {
                switch (weather_info_list[i].icon) {
                    case  1: weather_jp[i] = "ﾊﾚ"; break;
                    case  2: weather_jp[i] = "ﾊﾚ"; break;
                    case  3: weather_jp[i] = "ｸﾓ"; break;
                    case  4: weather_jp[i] = "ｸﾓ"; break;
                    case  9: weather_jp[i] = "ｱﾒ"; break;
                    case 10: weather_jp[i] = "ｱﾒ"; break;
                    case 11: weather_jp[i] = "ﾗｲ"; break;
                    case 13: weather_jp[i] = "ﾕｷ"; break;
                    case 50: weather_jp[i] = "ｷﾘ"; break;
                    case 90: weather_jp[i] = "ｽﾅ"; break;
                    default: weather_jp[i] = "??"; break;
                }
            }
            sprintf(line2, "%s%s %s %s %s %s %s",
                line2_first,
                weather_jp[0],
                weather_jp[1],
                weather_jp[2],
                weather_jp[3],
                weather_jp[4],
                weather_jp[5]);
            sprintf(line4, "%s%s %s %s %s %s %s",
                line4_first,
                weather_jp[6],
                weather_jp[7],
                weather_jp[8],
                weather_jp[9],
                weather_jp[10],
                weather_jp[11]);
        } else if (disp_mode == mode_temp) {
            line1_first = "ｵ ";
            line2_first = "ﾝ ";
            line3_first = "ﾄﾞ";
            line4_first = "  ";
            sprintf(line2, "%s%2d %2d %2d %2d %2d %2d",
                line2_first,
                weather_info_list[0].temperature,
                weather_info_list[1].temperature,
                weather_info_list[2].temperature,
                weather_info_list[3].temperature,
                weather_info_list[4].temperature,
                weather_info_list[5].temperature);
            sprintf(line4, "%s%2d %2d %2d %2d %2d %2d",
                line4_first,
                weather_info_list[6].temperature,
                weather_info_list[7].temperature,
                weather_info_list[8].temperature,
                weather_info_list[9].temperature,
                weather_info_list[10].temperature,
                weather_info_list[11].temperature);
        } else if (disp_mode == mode_wind) {
            line1_first = "ｶ ";
            line2_first = "ｾﾞ";
            line3_first = "  ";
            line4_first = "  ";
            sprintf(line2, "%s%2d %2d %2d %2d %2d %2d",
                line2_first,
                weather_info_list[0].wind_speed,
                weather_info_list[1].wind_speed,
                weather_info_list[2].wind_speed,
                weather_info_list[3].wind_speed,
                weather_info_list[4].wind_speed,
                weather_info_list[5].wind_speed);
            sprintf(line4, "%s%2d %2d %2d %2d %2d %2d",
                line4_first,
                weather_info_list[6].wind_speed,
                weather_info_list[7].wind_speed,
                weather_info_list[8].wind_speed,
                weather_info_list[9].wind_speed,
                weather_info_list[10].wind_speed,
                weather_info_list[11].wind_speed);
        } else if (disp_mode == mode_rain) {
            line1_first = "ｱ ";
            line2_first = "ﾒ ";
            line3_first = "  ";
            line4_first = "  ";
            int rain_level[weather_info_count] = {0};
            for (int i=0; i<weather_info_count; i++) {
                double rain_1h_mm = weather_info_list[i].rain_1h_mm;
#if defined(USE_TENKI_JP) || defined(USE_WEATHERNEWS)
                // Add level correction for rain forecast to round down the decimal point. (specifications of tenkijp and weathernews)
                if (weather_info_list[i].icon==9 or weather_info_list[i].icon==10) {
                    rain_1h_mm += 0.1;
                }
#endif
                if (rain_1h_mm < 0.1) {
                    rain_level[i] = 0;
                } else if (0.1 <= rain_1h_mm && rain_1h_mm < 0.5) {
                    rain_level[i] = 1;
                } else if (0.5 <= rain_1h_mm && rain_1h_mm < 1.0) {
                    rain_level[i] = 2;
                } else if (1.0 <= rain_1h_mm && rain_1h_mm < 4.0) {
                    rain_level[i] = 3;
                } else if (4.0 <= rain_1h_mm && rain_1h_mm < 7.5) {
                    rain_level[i] = 4;
                } else /*7.5<=rain_1h_mm*/ {
                    rain_level[i] = 5;
                }
            }
            sprintf(line2, "%sL%d L%d L%d L%d L%d L%d",
                line2_first,
                rain_level[0],
                rain_level[1],
                rain_level[2],
                rain_level[3],
                rain_level[4],
                rain_level[5]);
            sprintf(line4, "%sL%d L%d L%d L%d L%d L%d",
                line4_first,
                rain_level[6],
                rain_level[7],
                rain_level[8],
                rain_level[9],
                rain_level[10],
                rain_level[11]);
        }
        sprintf(line1, "%s%02d-%02d-%02d-%02d-%02d-%02d",
            line1_first,
            weather_info_list[0].hour,
            weather_info_list[1].hour,
            weather_info_list[2].hour,
            weather_info_list[3].hour,
            weather_info_list[4].hour,
            weather_info_list[5].hour);
        sprintf(line3, "%s%02d-%02d-%02d-%02d-%02d-%02d",
            line3_first,
            weather_info_list[6].hour,
            weather_info_list[7].hour,
            weather_info_list[8].hour,
            weather_info_list[9].hour,
            weather_info_list[10].hour,
            weather_info_list[11].hour);
    }
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(ConvStr(line1));
    lcd.setCursor(0, 1);
    lcd.print(ConvStr(line2));
    lcd.setCursor(0, 2);
    lcd.print(ConvStr(line3));
    lcd.setCursor(0, 3);
    lcd.print(ConvStr(line4));

#if defined(DEBUG_WITHOUT_PERIPHERAL_DEVICE)
    Serial.println(line1);
    Serial.println(line2);
    Serial.println(line3);
    Serial.println(line4);
#endif
}

void loop()
{
    if ((WiFi.status() == WL_CONNECTED)) {
        if (timer_wether_update==0) {
            lcd.clear();
            lcd.setCursor(0, 0);
            lcd.print(ConvStr("ﾃﾞｰﾀｺｳｼﾝﾁｭｳ...  "));
#if defined USE_TENKI_JP
            lcd.setCursor(0, 1);
            lcd.print(ConvStr(String(tenki_jp_host) + String(" ") + place_name + "       "));
            bool ret = update_weather_3h_tenki_jp();
#elif defined(USE_WEATHERNEWS)
            lcd.setCursor(0, 1);
            lcd.print(ConvStr(String(weathernews_host)));
            lcd.setCursor(0, 2);
            lcd.print(ConvStr(place_name));
            Serial.println("update_weather_1h_weathernews before.");
            bool ret = update_weather_1h_weathernews();
#else
            lcd.setCursor(0, 1);
            lcd.print(ConvStr(String("OWM. ") + zip_code));
            bool ret = update_weather_3h_owm();
#endif
            if (ret) {
                digitalWrite(LED_BUILTIN, LOW);
            } else {
                digitalWrite(LED_BUILTIN, HIGH);
            }
            timer_display_mode = 0;
            delay(100);
        }
        timer_wether_update = (timer_wether_update+1)%interval_wether_update;

        if (timer_display_mode==0) {
#if defined(USE_WEATHERNEWS)
            display_weather_info_1h(curr_display_mode);
#else
            display_weather_info_3h(curr_display_mode);
#endif
            curr_display_mode++;
            if (mode_end<=curr_display_mode) {
                curr_display_mode = mode_date;
            }
        }
        timer_display_mode = (timer_display_mode+1)%interval_display_mode;
    } else {
        lcd.setCursor(0, 0);
        lcd.print(ConvStr("ｴﾗｰ:Wifiｾﾂﾀﾞﾝ   "));
        lcd.setCursor(0, 1);
        lcd.print(ConvStr("    ﾘｾｯﾄｼﾏｽ... "));
        delay(5000);
        esp_restart();
    }

    //wait 50ms*20=1sec
    for (int i=0; i<20; i++) {
        delay(50);
        if (digitalRead(SW_PIN)==SW_ON) {
            timer_backlight_off = interval_backlight_off;
            lcd.backlight();
            timer_display_mode = 0;
        }
    }
#if defined(DEBUG_WITHOUT_PERIPHERAL_DEVICE)
    timer_display_mode = 0;
#endif
    //backlight auto off
    if (timer_backlight_off > 0) {
        timer_backlight_off--;
        if (timer_backlight_off==0) {
            lcd.noBacklight();
        }
    }

    // watchdog timer reset
    timerWrite(wdtimer, 0); 
}
