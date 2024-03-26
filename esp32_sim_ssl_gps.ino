#define TINY_GSM_MODEM_SIM7600
#define TINY_GSM_RX_BUFFER 1024 // Set RX buffer to 1Kb
// Set serial for debug console (to Serial Monitor, default speed 115200)
#define SerialMon Serial
#define SerialAT Serial1

// See all AT commands, if wanted
#define DUMP_AT_COMMANDS

// set GSM PIN, if any
#define GSM_PIN ""

// Your GPRS credentials, if any
const char apn[]  = "ibox.tim.it";
const char gprsUser[] = "";
const char gprsPass[] = "";

const char* ntpServerUrl = "https://worldtimeapi.org/api/timezone/Europe/Rome";

#include <TinyGsmClient.h>
#include <SPI.h>
#include <SD.h>
#include <Ticker.h>
#include <ArduinoHttpClient.h>
#include "SSLClient.h"
#include "TimeLib.h"
#include <NTPClient.h>
#include <ArduinoJson.h>
#include <ESP32Time.h>
#include <esp_sleep.h>

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */

#define UART_BAUD   115200
#define PIN_DTR     25
#define PIN_TX      27
#define PIN_RX      26
#define PWR_PIN     4

#define SD_MISO     2
#define SD_MOSI     15
#define SD_SCLK     14
#define SD_CS       13
#define LED_PIN     12


TinyGsm modem(SerialAT);
TinyGsmClient base_client(modem, 0);
SSLClient secure_layer(&base_client);

String coord = "";
String id = "001";

int counter, lastIndex, numberOfPieces = 24;
String pieces[24], input;

const char server[] = "your_domain_name"; // domain name: example.com, maker.ifttt.com, etc
const char resource[] = "/api/postgps";  // resource path, for example: /post-data.php
const int port = 9000;

tmElements_t my_time;  // time elements structure
time_t unix_timestamp = 1700664599; // a timestamp

HttpClient client = HttpClient(secure_layer, server, port);
HttpClient http(base_client, ntpServerUrl);

int offset = 3600;
ESP32Time rtc(offset);

//Write your SSL certificate
const char* root_ca = 
"-----BEGIN CERTIFICATE-----\n"
"MIIDkzCCAnugAwIBAgIILDB38OxwmWEwDQYJKoZIhvcNAQELBQAwdzELMAkGA1UE\n"
"BhMCSVQxEDAO.....................EXCETERA\n"
"-----END CERTIFICATE-----\n";

void print_wakeup_reason()
{
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason)
  {
  case ESP_SLEEP_WAKEUP_EXT0:
    Serial.println("Wakeup caused by external signal using RTC_IO");
    break;
  case ESP_SLEEP_WAKEUP_EXT1:
    Serial.println("Wakeup caused by external signal using RTC_CNTL");
    break;
  case ESP_SLEEP_WAKEUP_TIMER:
    Serial.println("Wakeup caused by timer");
    break;
  case ESP_SLEEP_WAKEUP_TOUCHPAD:
    Serial.println("Wakeup caused by touchpad");
    break;
  case ESP_SLEEP_WAKEUP_ULP:
    Serial.println("Wakeup caused by ULP program");
    break;
  default:
    Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
    break;
  }
}

void setup(){

  // Set console baud rate
  Serial.begin(115200);
  delay(10);
  
  // Set LED OFF
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, HIGH);
  delay(300);
  digitalWrite(PWR_PIN, LOW);

  delay(1000);
  
  SerialAT.begin(UART_BAUD, SERIAL_8N1, PIN_RX, PIN_TX);

  // Restart takes quite some time
  // To skip it, call init() instead of restart()
  Serial.println("Initializing modem...");
  if (!modem.restart()) {
    Serial.println("Failed to restart modem, attempting to continue without restarting");
  }

  String name = modem.getModemName();
  delay(500);
  Serial.println("Modem Name: " + name);

  String modemInfo = modem.getModemInfo();
  delay(500);
  Serial.println("Modem Info: " + modemInfo);
  
  // Unlock your SIM card with a PIN if needed
  if ( GSM_PIN && modem.getSimStatus() != 3 ) {
      modem.simUnlock(GSM_PIN);
  }
  modem.sendAT("+CFUN=0 ");
  if (modem.waitResponse(10000L) != 1) {
    DBG(" +CFUN=0  false ");
  }
  delay(200);

  
  /*
    2 Automatic
    13 GSM only
    38 LTE only
    51 GSM and LTE only
  * * * */


  String res;
  // CHANGE NETWORK MODE, IF NEEDED
  res = modem.setNetworkMode(2);
  if (res != "1") {
    DBG("setNetworkMode  false ");
    return ;
  }
  delay(200);

  Serial.println("\n\n\nWaiting for network...");
  if (!modem.waitForNetwork()) {
    delay(10000);
    //return;
  }

  if (modem.isNetworkConnected()) {
    Serial.println("Network connected");
  }

    // --------TESTING GPRS--------
  Serial.println("\n---Starting GPRS TEST---\n");
  Serial.println("Connecting to: " + String(apn));
  if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
    delay(10000);
    return;
  }

  Serial.print("GPRS status: ");
  if (modem.isGprsConnected()) {
    Serial.println("connected");
  } else {
    Serial.println("not connected");
  }

  String ccid = modem.getSimCCID();
  Serial.println("CCID: " + ccid);

  String imei = modem.getIMEI();
  Serial.println("IMEI: " + imei);

  String cop = modem.getOperator();
  Serial.println("Operator: " + cop);

  IPAddress local = modem.localIP();
  Serial.println("Local IP: " + String(local));

  int csq = modem.getSignalQuality();
  Serial.println("Signal quality: " + String(csq));

  SerialAT.println("AT+CPSI?");     //Get connection type and band
  delay(500);
  if (SerialAT.available()) {
    String r = SerialAT.readString();
    Serial.println(r);
  }

  Serial.println("\n---End of GPRS TEST---\n");

    // --------TESTING GPS--------

  Serial.println("\n---Starting GPS TEST---\n");
  // Set SIM7000G GPIO4 HIGH ,turn on GPS power
  // CMD:AT+SGPIO=0,4,1,1
  // Only in version 20200415 is there a function to control GPS power
  modem.sendAT("+SGPIO=0,4,1,1");
  if (modem.waitResponse(10000L) != 1) {
    DBG(" SGPIO=0,4,1,1 false ");
  }

  Serial.println("\n---End of GPRS TEST---\n");

  secure_layer.setCACert(root_ca);

  modemPowerOn();
  
}

void modemPowerOn(){
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, LOW);
  delay(1000);
  digitalWrite(PWR_PIN, HIGH);
}

void modemPowerOff(){
  pinMode(PWR_PIN, OUTPUT);
  digitalWrite(PWR_PIN, LOW);
  delay(1500);
  digitalWrite(PWR_PIN, HIGH);
}


void modemRestart(){
  modemPowerOff();
  delay(1000);
  modemPowerOn();
}

void loop(){

  rtc.setTime(unix_timestamp);

  Serial.println("DATA DELL'RTC: ");
  Serial.println(rtc.getDateTime());

  print_wakeup_reason();

  if((rtc.getHour(true)>21 || rtc.getHour(true)<5) && (rtc.getDayofWeek()!=0 && rtc.getDayofWeek()!=6)){
    Serial.println("Sleep Mode");
    esp_sleep_enable_timer_wakeup(25200000000); //7 ore //25200000000    //(540 * 60 * 1000000); //9 ore
    esp_deep_sleep_start();
  }else if(rtc.getDayofWeek()==6 && (rtc.getHour(true)>14)){
    Serial.println("Sleep Mode");
    esp_sleep_enable_timer_wakeup(36000000000); //10 ore
    esp_deep_sleep_start();
  }else if(rtc.getDayofWeek()==0){
    Serial.println("Sleep Mode");
    esp_sleep_enable_timer_wakeup(100800000000); //28 ore
    esp_deep_sleep_start();
  }

  String res;
  Serial.println("data ora dell'rtc: ");
  Serial.println(rtc.getDateTime());
  Serial.println("========INIT========");

  if (!modem.init()) {
    modemRestart();
    delay(2000);
    Serial.println("Failed to restart modem, attempting to continue without restarting");
    return;
  }

  Serial.println("========SIMCOMATI======");
  modem.sendAT("+SIMCOMATI");
  modem.waitResponse(1000L, res);
  res.replace(GSM_NL "OK" GSM_NL, "");
  Serial.println(res);
  res = "";
  Serial.println("=======================");

  Serial.println("=====Preferred mode selection=====");
  modem.sendAT("+CNMP?");
  if (modem.waitResponse(1000L, res) == 1) {
    res.replace(GSM_NL "OK" GSM_NL, "");
    Serial.println(res);
  }
  res = "";
  Serial.println("=======================");


  Serial.println("=====Preferred selection between CAT-M and NB-IoT=====");
  modem.sendAT("+CMNB?");
  if (modem.waitResponse(1000L, res) == 1) {
    res.replace(GSM_NL "OK" GSM_NL, "");
    Serial.println(res);
  }
  res = "";
  Serial.println("=======================");


  String name = modem.getModemName();
  Serial.println("Modem Name: " + name);

  String modemInfo = modem.getModemInfo();
  Serial.println("Modem Info: " + modemInfo);

  // Unlock your SIM card with a PIN if needed
  if ( GSM_PIN && modem.getSimStatus() != 3 ) {
    modem.simUnlock(GSM_PIN);
  }

  for (int i = 0; i <= 4; i++) {
    uint8_t network[] = {
        2,  //Automatic
        13, //GSM only
        38, //LTE only
        51  //GSM and LTE only
    };
    Serial.printf("Try %d method\n", network[i]);
    modem.setNetworkMode(network[i]);
    delay(3000);
    bool isConnected = false;
    int tryCount = 60;
    while (tryCount--) {
      int16_t signal =  modem.getSignalQuality();
      Serial.print("Signal: ");
      Serial.print(signal);
      Serial.print(" ");
      Serial.print("isNetworkConnected: ");
      isConnected = modem.isNetworkConnected();
      Serial.println( isConnected ? "CONNECT" : "NO CONNECT");
      if (isConnected) {
        break;
      }
      delay(1000);
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
    if (isConnected) {
        break;
    }
  }
  digitalWrite(LED_PIN, HIGH);

  Serial.println();
  Serial.println("Device is connected .");
  Serial.println();

  Serial.println("=====Inquiring UE system information=====");
  modem.sendAT("+CPSI?");
  if (modem.waitResponse(1000L, res) == 1) {
    res.replace(GSM_NL "OK" GSM_NL, "");
    Serial.println(res);
  }

  // Set SIM7000G GPIO4 HIGH ,turn on GPS power
  // CMD:AT+SGPIO=0,4,1,1
  // Only in version 20200415 is there a function to control GPS power
  modem.sendAT("+SGPIO=0,4,1,1");
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println(" SGPIO=0,4,1,1 false ");
  }

  modem.enableGPS();
  
  delay(15000);
  float lat      = 0;
  float lon      = 0;
  float speed    = 0;
  float alt     = 0;
  int   vsat     = 0;
  int   usat     = 0;
  float accuracy = 0;
  int   year     = 0;
  int   month    = 0;
  int   day      = 0;
  int   hour     = 0;
  int   min      = 0;
  int   sec      = 0;
  
  for (int8_t i = 15; i; i--) {
    SerialMon.println("Requesting current GPS/GNSS/GLONASS location");
    if (modem.getGPS(&lat, &lon, &speed, &alt, &vsat, &usat, &accuracy,
                     &year, &month, &day, &hour, &min, &sec)) {
      SerialMon.println("Latitude: " + String(lat, 8) + "\tLongitude: " + String(lon, 8));
      SerialMon.println("Speed: " + String(speed) + "\tAltitude: " + String(alt));
      SerialMon.println("Visible Satellites: " + String(vsat) + "\tUsed Satellites: " + String(usat));
      SerialMon.println("Accuracy: " + String(accuracy));
      SerialMon.println("Year: " + String(year) + "\tMonth: " + String(month) + "\tDay: " + String(day));
      SerialMon.println("Hour: " + String(hour) + "\tMinute: " + String(min) + "\tSecond: " + String(sec));

      // convert a date and time into unix time, offset 1970
      my_time.Second = sec;
      my_time.Hour = hour;
      my_time.Minute = min;
      my_time.Day = day;
      my_time.Month = month;      // months start from 0, so deduct 1
      my_time.Year = year - 1970; // years since 1970, so deduct 1970
    
      unix_timestamp =  makeTime(my_time);
      Serial.println("Unix Timestamp: ");
      Serial.println(unix_timestamp);

      char buffer1[12];
      char buffer2[12];
      
      dtostrf(lat, 10, 6, buffer1);
      String lati = String(buffer1);

      dtostrf(lon, 10, 6, buffer2);
      String longi = String(buffer2);
      
    }
    
    else {
      SerialMon.println("Couldn't get GPS/GNSS/GLONASS location, retrying in 10s.");
      delay(10000L);
    }
  }
  SerialMon.println("Retrieving GPS/GNSS/GLONASS location again as a string");
  String gps_raw = modem.getGPSraw();
  SerialMon.println("GPS/GNSS Based Location String: " + gps_raw);
  SerialMon.println("Disabling GPS");
  modem.disableGPS();

  // Set SIM7000G GPIO4 LOW ,turn off GPS power
  // CMD:AT+SGPIO=0,4,1,0
  // Only in version 20200415 is there a function to control GPS power
  modem.sendAT("+SGPIO=0,4,1,0");
  if (modem.waitResponse(10000L) != 1) {
    SerialMon.println(" SGPIO=0,4,1,0 false ");
  }

  delay(200);

  if(!modem.isGprsConnected()){
     if (!modem.gprsConnect(apn, gprsUser, gprsPass)) {
      SerialMon.println("GPRS FALLITO");
      delay(2000);
      return;
     }
     SerialMon.println(" GPRS SUCCESSO");
     
  }else{
    
    if(modem.isNetworkConnected() && modem.isGprsConnected()){
    
      Serial.println("IL MODEM E CONNESSO");
      
            // Make a HTTPS POST request:
      Serial.println("Making POST request securely");
      String contentType = "application/text";
      String postData = coord;        
      client.post(resource, contentType, postData);
      int status_code = client.responseStatusCode();
      String response = client.responseBody();
      
      Serial.print("Status code: ");
      Serial.println(status_code);
      Serial.print("Response: ");
      Serial.println(response);

      client.stop();
    
    }else{
      Serial.println("IL MODEM NON E CONNESSO........");
    }
  
  delay(40000); // Attendere 40 secondi prima di inviare il prossimo dato
  }
}
