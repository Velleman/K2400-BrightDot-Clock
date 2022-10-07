/*
 * Clock firmware 1.0
 * 
 * Written by JGE
 * 
 * 2018
 */
//-------------------------------------------------------------------------------
#include "clock_libs.h"
#include <WiFi.h>
#include <FS.h>

#include <time.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
//-------------------------------------------------------------------------------
#define RECONINTERVAL 10
//#define DEBUG                                     //uncomment to enable debug messages over serial

#ifdef DEBUG
 #define DEBUG_PRINT(x)     Serial.print (x)
 #define DEBUG_PRINTDEC(x)  Serial.print (x, DEC)
 #define DEBUG_PRINTLN(x)   Serial.println (x)
#else
 #define DEBUG_PRINT(x)
 #define DEBUG_PRINTDEC(x)
 #define DEBUG_PRINTLN(x) 
#endif 
//-------------------------------------------------------------------------------
Preferences preferences;                          //Preferences class
//-------------------------------------------------------------------------------
TaskHandle_t LEDtask;                             //Handle name for LEDtask pinned to core
//-------------------------------------------------------------------------------
const char *ssidown = "My K2400";                 //SSID for when device acts  as AP
const char *passwordown = "K2400PWD";        //password for when deice acts  as AP
AsyncWebServer server(80);                        //Creating webserver instance
//-------------------------------------------------------------------------------
const char * udpAddress = "255.255.255.255";      //UDP broadcast for discover app
const int udpPort = 30303;                        //Port for UDP broadcast
WiFiUDP udp;                                      //create UDP instance
struct tm timeinfo;                               //Timeinfo will hold all current time data
String NTPstring = "time.google.com";             //Webaddress for timeserver
String NTPstringbackup;                           //backup string for when NTP server is reset
float gmtOffset_hour = 0;                         //Greenwich Mean Time offset (in hours)
float daylightOffset_hour = 0;                    //Daylight Saving Time offset (in seconds)
//-------------------------------------------------------------------------------
const uint16_t PixelCount = 60;                   //Number of pixels/LEDS in the clock
const uint8_t PixelPin = 2;                       //Physical data pin to which the LED strip is connected

bool pendantDir = true;                           //Direction of pendant animation           
int pendantTail = 5;                              //Fadeout number of the pendant tail

int secondHandTail = 1;                           //Fadeout number of the second hand tail

//bool fadeDir = true;                            //Fade direction for five minute marks           
//bool fadeDir2 = true;                           //Fade direction for quarter marks

int state = 0;                                    //Holds the global state for the clock (first start-up, connected, not connected).

int animationState = 0;                           //Holds the the animation state for the clock, what animations need to be displayed

int totalBrightness = 255;                        //Holds total brightness value
int maxBrightness = 255;                          //Holds the maximum brightness value (to insure no current overload), total brightness gets mapped to this value... This value changes between displaymodes to achieve max possible brightness (some are heavier than others)

int checkPeriod = 1000;                           //Variable for timechecking delay
unsigned long checkTime_now = 0;                  //Variable for timechecking delay

String validationString;                          //String that wil hold data to be validated
bool validation = false;                          //bool that will be false when validation is not OK

int secCalc = 0;                                  //0-59 global second data gets updated every second
int minCalc = 0;                                  //0-59 global minutes data gets updated every second
int hourCalc = 0;                                 //0-59 global hour data gets updated every second (to show intermediate hour position)
int hourDirectCalc = 0;                           //0-23 global hour data gets updated every second

bool WLANOn = false;                              //Are WIFI preferences stored or not?
String SSIDstring;                                //SSID string of network the clock will try to connect with. Max. length 32 char
String passwordstring;                            //password string of network the clock will try to connect with. Max. length 63 char
String deviceName;                                //name of the device (can be changed by user). Max. length 16 char

int dimOn = 0;                                    //Is auto dimming on or off?
int hourStartDim = 0;                             //Start hour of auto dim feature
int hourStopDim = 0;                              //Stop hour of auto dim feature
int valueDim = 0;                                 //Value to dim to
int dimBrightness = 255;                          //Default value

int alarmOn = 0;                                  //Is alarm on or off?
int minAlarm = 0;                                 //Start minute of alarm
int hourAlarm = 0;                                //Start hour of alarm
#define DURATIONALARM 60                          //How long does the alarm last
int durationAlarm;                                //variable for counting down

int invert = 0;                                   //Inverts all the colors on the clockface

bool smartConfig = 0;                             //switches smartconfig on or off (default is off as it needs the ESPRESSIF app to work)

int reconnectCounter = 0;

RgbColor pendantColor(0,0,0);                     //Default color of the pendant            
RgbColor fiveMinuteColor(0,0,0);                  //Default color of the 5 minutes indication 
RgbColor quarterColor(0,0,0);                     //Default color of the 15 minutes indication 
RgbColor secondHandColor(0,0,255);                //Default color of the second hand
RgbColor minuteHandColor(0,255,0);                //Default color of the minute hand
RgbColor hourHandColor(255,0,0);                  //Default color of the hour hand

RgbColor animationArray1[60];                     //Different arrays to store the animations before blending
RgbColor animationArray2[60];
RgbColor animationArray3[60];
RgbColor animationArray4[60];
RgbColor animationArray5[60];
RgbColor animationArray6[60];
RgbColor animationArray7[60];

int circleArray[61] = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,57,58,59,0};
int fiveMinuteNumbers[] = {0,5,10,15,20,25,30,35,40,45,50,55};
int quarterNumbers[] = {0,15,30,45};
//-------------------------------------------------------------------------------
NeoPixelBus<NeoGrbFeature, NeoEsp32RmtWS2813_V3Method> strip(PixelCount, PixelPin);
NeoGamma<NeoGammaTableMethod> colorGamma;
NeoPixelAnimator animations(12);
AnimEaseFunction animation1 = NeoEase::QuadraticInOut;
AnimEaseFunction animation2 = NeoEase::QuadraticInOut;
AnimEaseFunction animation3 = NeoEase::QuadraticInOut;
AnimEaseFunction animation4 = NeoEase::Linear;
AnimEaseFunction animation5 = NeoEase::QuadraticIn;
//-------------------------------------------------------------------------------
/*  
 *   setup starts serial communication, 
 *  starts task that is run on seperate core for the LEDs, 
 *  Init's Wlan stuff, 
 *  and syncs time with NTP server for the first time when WLAN stuff has been handled.
 */
//-------------------------------------------------------------------------------
void setup(){
  
  Serial.begin(115200);
  DEBUG_PRINTLN();
  DEBUG_PRINTLN("Booted");
  
  NTPstringbackup = NTPstring;   
  
  strip.Begin();
  strip.Show();

  LoadPreferences();

  delay(500);

  xTaskCreatePinnedToCore(
    codeForLEDtask,               /* Task Function */
    "LEDtask",                    /* Name of task */
    1000,                         /* Stack size of task */
    NULL,                         /* Parameter of the task */
    1,                            /* Priority of the Task */
    &LEDtask,                     /* Task handle */
    0);                           /* Core */

  WLANInit();

  delay(500);

  //init and get the time
  SyncWithServer();
  CheckLocalTime();

  delay(500);
  
  //This initializes udp and transfer buffer
  udp.begin(udpPort);

  delay(500);
}
//-------------------------------------------------------------------------------
/*  
 *  codeForLEDtask is the only task running on core 1.
 *  It controls all things LED related (animations, output, etc.)
 */
//-------------------------------------------------------------------------------
void codeForLEDtask( void * parameter){
  int currentAnimationState;
 
  for(;;){
    switch (state) {
      //First start-up
      case 0:
        for (int i=0; i <= 59; i++){
          strip.SetPixelColor(i, RgbColor (0,0,255));
          strip.Show();
          delay(16);
          if( state != 0){break;}
        }
        for (int i=0; i <= 59; i++){
          strip.SetPixelColor(i, RgbColor (0,0,0));
          strip.Show();
          delay(16);
          if( state != 0){break;}
        }
        break;
      //Unconnected
      case 1:
        for (int i=0; i <= 59; i++){
          strip.SetPixelColor(i, RgbColor (0,255,0));
          strip.Show();
          delay(16);
          if( state != 1){break;}
        }
        for (int i=0; i <= 59; i++){
          strip.SetPixelColor(i, RgbColor (0,0,0));
          strip.Show();
          delay(16);
          if( state != 1){break;}
        }
        break;
      //Connected
      case 2:
        currentAnimationState = animationState;
    
        switch (animationState) {
        case 0:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(6,10,    HourHand0);
          animations.StartAnimation(5,10,    MinuteHand0);
          animations.StartAnimation(4,25,    SecondHand0Tail);
          animations.StartAnimation(3,10,    SecondHand0);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        case 1:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(10,10,   HourHand1);
          animations.StartAnimation(9,10,    MinuteHand1);
          animations.StartAnimation(8,10,    SecondHand1);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        case 2:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(10,10,   HourHand2);
          animations.StartAnimation(9,10,    MinuteHand2);
          animations.StartAnimation(8,1000,  SecondHand2);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        case 3:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(10,10,   HourHand3);
          animations.StartAnimation(9,10,    MinuteHand3);
          animations.StartAnimation(8,10,    SecondHand3);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        case 4:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(10,1000, HourHand4);
          animations.StartAnimation(9,1000,  MinuteHand4);
          animations.StartAnimation(8,1000,  SecondHand4);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        case 5:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(10,10,   HourHand5);
          animations.StartAnimation(9,10,    MinuteHand5);
          animations.StartAnimation(8,10,    SecondHand5);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        case 6:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(10,1000, HourHand6);
          animations.StartAnimation(9,1000,  MinuteHand6);
          animations.StartAnimation(8,1000,  SecondHand6);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        case 7:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(10,1000, HourHand7);
          animations.StartAnimation(9,1000,  MinuteHand7);
          animations.StartAnimation(8,1000,  SecondHand7);
          animations.StartAnimation(4,5,     AllTails);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        case 8:
          AnimSwitch();
          //maxBrightness = 230;
          animations.StartAnimation(10,1000, HourHand8);
          animations.StartAnimation(9,1000,  MinuteHand8);
          animations.StartAnimation(8,1000,  SecondHand8);
          animations.StartAnimation(4,5,     AllTails);
          animations.StartAnimation(7,10,    QuarterMarks);
          animations.StartAnimation(2,10,    FiveMinuteMarks);
          animations.StartAnimation(1,10,    Pendanttail);
          animations.StartAnimation(0,1000,  Pendant);
          break;
        default:
          animationState = 0;
        }
        
        while((state == 2) && currentAnimationState == animationState){
          animations.UpdateAnimations();
          BlendArrays();
          strip.Show();
        }
        break;
    }
  }
}
//-------------------------------------------------------------------------------
/*  
 *  CheckAutoDim checks if all requirements are met to start auto dimming.
 *  Auto Dimming is diming the total brightness for a predefined time from a predefined start time, 
 *  for example at night the clock does not need to be very bright.
 */
//-------------------------------------------------------------------------------
void CheckAutoDim(void){

 if(dimOn)
  {
    if((hourDirectCalc > hourStartDim) || (hourDirectCalc < hourStopDim)){
      dimBrightness = valueDim;
    }
    else{
      dimBrightness = 255;
    }
  }
  else{
    dimBrightness = 255;
  }
}
//-------------------------------------------------------------------------------
/*  
 *  CheckAlarm checks if all requirements are met to start the alarm.
 */
//-------------------------------------------------------------------------------
void CheckAlarm(void){  
  if(alarmOn)
  {
    if((hourDirectCalc == hourAlarm) && (minCalc == minAlarm) && (secCalc == 0)){
      durationAlarm = DURATIONALARM;
      animations.StartAnimation(11,1000,AlarmAnimation);
      DEBUG_PRINTLN("");
      DEBUG_PRINTLN("Alarm triggered");
      DEBUG_PRINTLN("");
    }
  }
}
//-------------------------------------------------------------------------------
/*  
 *  AnimSwitch makes sure all the animation are stopped, all the animation arrays are empty and updates the time global vars.
 *  Should be called when you switch between main animations.
 */
//-------------------------------------------------------------------------------
void AnimSwitch(void){
  animations.StopAll();
  ClearArrays();
  ProcessTime();
}
//-------------------------------------------------------------------------------
/*  
 *  LoadPreferences checks if any preferences are stored and wil load these accordingly. Is called on setup.
 */
//-------------------------------------------------------------------------------
void LoadPreferences(void){
  preferences.begin("WLAN", false);
  if(preferences.getBool("WLANStored", 0)){
    LoadWLAN();
  }
  preferences.end();

  preferences.begin("alarm", false);
  if(preferences.getBool("alarmStored", 0)){
    LoadAlarm();
  }
  preferences.end();

  preferences.begin("dim", false);
  if(preferences.getBool("dimStored", 0)){
    LoadDim();
  }
  preferences.end();

  preferences.begin("color", false);
  if (preferences.getUInt("colorStored", 0)){
    LoadColor();
  }
  preferences.end();
  
  preferences.begin("NTP", false);
  if (preferences.getUInt("NTPStored", 0)){
    LoadNTP();
  }
  else {
    SaveNTP();
  }
  preferences.end();
  
}
//-------------------------------------------------------------------------------
/*  
 *  WLANInit takes care of the WLAN setup. When first startup it will put the device in AP mode. 
 *  When stored WLAN credentials are present it will try to connect to this network.
 *  Upon a press of the WLANreset button it will remove all stored credentials and restart the device.
 */
//-------------------------------------------------------------------------------
void WLANInit(void){
  
  if(WLANOn && digitalRead(0)){
    state = 1;
    
    while ((WiFi.status() != WL_CONNECTED)) {
      if(millis() > checkTime_now + checkPeriod){
        checkTime_now = millis();
  
        if (reconnectCounter > RECONINTERVAL){
          reconnectCounter = 0;
        }
        
        if(reconnectCounter == 0){ 
          DEBUG_PRINTLN("Trying to connect to ");
          DEBUG_PRINT(SSIDstring);
          DEBUG_PRINTLN(" using stored credentials");
      
          char cssid[SSIDstring.length()+1];
          char cpass[passwordstring.length()+1];
      
          SSIDstring.toCharArray(cssid, SSIDstring.length()+1);
          passwordstring.toCharArray(cpass, passwordstring.length()+1);
          
          WiFi.begin(cssid, cpass);
        }
        reconnectCounter++;
        DEBUG_PRINT("."); 
      }
      if(!digitalRead(0)){
        ResetWLAN();
        RestartDevice();
      }
    }

    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("");
    DEBUG_PRINTLN("WLAN connected");
    DEBUG_PRINT("IP address is: ");
    DEBUG_PRINTLN(WiFi.localIP());
    DEBUG_PRINTLN("");
  }
  else{
    state = 0;
    ResetWLAN();
    
    if(smartConfig == true){
      DEBUG_PRINTLN("Starting WLAN initialisation");
      //Init WLAN as Station, start SmartConfig
      WiFi.mode(WIFI_AP_STA);
      WiFi.beginSmartConfig();
    
      //Wait for SmartConfig packet from mobile
      DEBUG_PRINTLN("Waiting for SmartConfig.");
      
      DEBUG_PRINTLN("Waiting for WLAN");
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        DEBUG_PRINT(".");
      }
    
      DEBUG_PRINTLN("");
      DEBUG_PRINTLN("SmartConfig received.");
    
      //Wait for WLAN to connect to AP
      DEBUG_PRINTLN("Waiting for WLAN");
      while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        DEBUG_PRINT(".");
      }
    
      DEBUG_PRINT("IP Address: ");
      DEBUG_PRINTLN(WiFi.localIP());
      DEBUG_PRINTLN(" ");
    }
    else if (smartConfig == false)
    {
            
      DEBUG_PRINTLN("configuring AP...");
      WiFi.softAPConfig(IPAddress(192,168,0,1), IPAddress(192,168,0,1) ,IPAddress(255,255,255,0));

      delay(500);

      DEBUG_PRINTLN("configuring AP...");
      WiFi.softAPConfig(IPAddress(192,168,0,1), IPAddress(192,168,0,1) ,IPAddress(255,255,255,0));

      delay(500);

      DEBUG_PRINTLN("Acting as AP...");
      WiFi.softAP(ssidown, passwordown, 13, 0, 4);

      delay(500);
      
      DEBUG_PRINT("IP address is: ");
      DEBUG_PRINTLN(WiFi.softAPIP());
      DEBUG_PRINTLN("");

      server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){request->send(200, "text/html", HTML1);});

      server.on("/wifisave", HTTP_POST, [](AsyncWebServerRequest *request){
        if (ValidateWLAN(request)){
          SaveWLAN();
          request->send(200, "text/message", ""); 
          RestartDevice(); 
        }
        else{
          request->send(400, "text/message", "");
          DEBUG_PRINTLN("");
          DEBUG_PRINTLN("ERROR");
          DEBUG_PRINTLN("");
        }
      });
      
      MDNSService("K2400");
      
      server.begin();

      while(1){}
    }
  }

  MDNSService(deviceName);

  WebServer();

  state = 2;
}
//-------------------------------------------------------------------------------
/*  
 *  MDNSService makes sure that the device can be reaced via the .local webaddres on a apple device.
 */
//-------------------------------------------------------------------------------
void MDNSService(String URL){

   char cURL[URL.length()+1];
   URL.toCharArray(cURL, URL.length()+1);
   
  // Set up mDNS responder:
  if (!MDNS.begin(cURL)) {
      DEBUG_PRINTLN("Error setting up MDNS responder!");
      while(1) {
          delay(1000);
      }
  }
  DEBUG_PRINTLN("mDNS responder started");
  DEBUG_PRINTLN("Go to http://" + URL + ".local to connect...");
  DEBUG_PRINTLN("");

  // Add service to MDNS-SD
  MDNS.addService("http", "tcp", 80);
}
//-------------------------------------------------------------------------------
/*  
 *  ValidateNTP validates the incoming NTP data. 
 *  Stores the incoming data when correct.
 *  Returns false when something is wrong.
 */
//-------------------------------------------------------------------------------
bool ValidateNTP(AsyncWebServerRequest *request){
  int paramsNr = request->params();
  if(paramsNr == 3){
    
    AsyncWebParameter* p = request->getParam(0);
    NTPstring = p->value();

    p = request->getParam(1);
    String NTPData = p->value();
    gmtOffset_hour = NTPData.toFloat();

    p = request->getParam(2);
    NTPData = p->value();
    daylightOffset_hour = NTPData.toFloat();
    return true;
  }
  else {
    return false;
  }
}
//-------------------------------------------------------------------------------
/*  
 *  ValidateWLAN validates the incoming WLAN data. 
 *  Stores the incoming data when correct.
 *  Returns false when something is wrong.
 */
//-------------------------------------------------------------------------------
bool ValidateWLAN(AsyncWebServerRequest *request){
  int paramsNr = request->params();
  if(paramsNr == 3){
   
    AsyncWebParameter* p = request->getParam(0);
    validationString = p->value();
    if (validationString.length() <= 32){
      SSIDstring = validationString;
    }
    else{
      return false;
    }
    
    p = request->getParam(1);
    validationString = p->value();
    if (validationString.length() <= 63){
      passwordstring = validationString;
    }
    else{
      return false;
    }
    
    p = request->getParam(2);
    validationString = p->value();
    if (validationString.length() <= 16){
      deviceName = validationString;
    }
    else{
      return false;
    }
    
    return true;
  }
  else {
    return false;
  }
}
//-------------------------------------------------------------------------------
/*  
 *  ValidateAlarm validates the incoming Alarm data. 
 *  Stores the incoming data when correct.
 *  Returns false when something is wrong.
 */
//-------------------------------------------------------------------------------
bool ValidateAlarm(AsyncWebServerRequest *request){
  int paramsNr = request->params();
  if(paramsNr == 3){
   
    AsyncWebParameter* p = request->getParam(0);
    validationString = p->value();
    if ((validationString.toInt() == 0) || (validationString.toInt() == 1)){
      alarmOn = validationString.toInt();
    }
    else{
      return false;
    }
    
    p = request->getParam(1);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 23)){
      hourAlarm = validationString.toInt();
    }
    else{
      return false;
    }
    
    p = request->getParam(2);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 59)){
      minAlarm = validationString.toInt();
    }
    else{
      return false;
    }
    
    return true;
  }
  else {
    return false;
  }
}
//-------------------------------------------------------------------------------
/*  
 *  ValidateDim validates the incoming Auto Dim data. 
 *  Stores the incoming data when correct.
 *  Returns false when something is wrong.
 */
//-------------------------------------------------------------------------------
bool ValidateDim(AsyncWebServerRequest *request){
  int paramsNr = request->params();
  if(paramsNr == 4){
   
    AsyncWebParameter* p = request->getParam(0);
    validationString = p->value();
    if ((validationString.toInt() == 0) || (validationString.toInt() == 1)){
      dimOn = validationString.toInt();
    }
    else{
      return false;
    }
    
    p = request->getParam(1);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 23)){
      hourStartDim = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(2);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 23)){
      hourStopDim = validationString.toInt();
    }
    else{
      return false;
    }
    
    p = request->getParam(3);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      valueDim = validationString.toInt();
    }
    else{
      return false;
    }
    
    return true;
  }
  else {
    return false;
  }
}
//-------------------------------------------------------------------------------
/*  
 *  ValidateData validates the incoming Color data. 
 *  Stores the incoming data when correct.
 *  Returns false when something is wrong.
 */
//-------------------------------------------------------------------------------
bool ValidateData(AsyncWebServerRequest *request){
  int paramsNr = request->params();
  if(paramsNr == 21){
   
    AsyncWebParameter* p = request->getParam(0);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      totalBrightness = validationString.toInt();
    }
    else{
      return false;
    }
    
    p = request->getParam(1);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      hourHandColor.R = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(2);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      hourHandColor.G = validationString.toInt();
    }
    else{
      return false;
    }
    
    p = request->getParam(3);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      hourHandColor.B = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(4);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      minuteHandColor.R = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(5);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      minuteHandColor.G = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(6);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      minuteHandColor.B = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(7);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      secondHandColor.R = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(8);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      secondHandColor.G = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(9);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      secondHandColor.B = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(10);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      pendantColor.R = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(11);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      pendantColor.G = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(12);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      pendantColor.B = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(13);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      fiveMinuteColor.R = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(14);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      fiveMinuteColor.G = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(15);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      fiveMinuteColor.B = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(16);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      quarterColor.R = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(17);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      quarterColor.G = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(18);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      quarterColor.B = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(19);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 255)){
      animationState = validationString.toInt();
    }
    else{
      return false;
    }

    p = request->getParam(20);
    validationString = p->value();
    if ((validationString.toInt() >= 0) && (validationString.toInt() <= 1)){
      invert = validationString.toInt();
    }
    else{
      return false;
    }
    
    return true;
  }
  else {
    return false;
  }
}
//-------------------------------------------------------------------------------
/*  
 *  WebServer takes care of all the webrequests
 */
//-------------------------------------------------------------------------------
void WebServer(void){
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(200, "text/html", HTML2);
  });

  server.on("/ntpreset", HTTP_POST, [](AsyncWebServerRequest *request){
    ResetNTP();
    SyncWithServer(); 
    request->send(200, "text/message", "");
  });

  server.on("/ntpsave", HTTP_POST, [](AsyncWebServerRequest *request){
    
    if (ValidateNTP(request)){
      SaveNTP();
      SyncWithServer();  
      request->send(200, "text/message", "");
    }
    else{
      request->send(400, "text/message", "");
      DEBUG_PRINTLN("");
      DEBUG_PRINTLN("ERROR");
      DEBUG_PRINTLN("");
    }
  });
  
  server.on("/wifisave", HTTP_POST, [](AsyncWebServerRequest *request){
    if (ValidateWLAN(request)){
      SaveWLAN();
      request->send(200, "text/message", "");
      RestartDevice();  
    }
    else{
      request->send(400, "text/message", "");
      DEBUG_PRINTLN("");
      DEBUG_PRINTLN("ERROR");
      DEBUG_PRINTLN("");
    }
  });

  server.on("/wifireset", HTTP_POST, [](AsyncWebServerRequest *request){
    ResetWLAN();
    request->send(200, "text/message", "");
    RestartDevice(); 
  });

  server.on("/alarmsave", HTTP_POST, [](AsyncWebServerRequest *request){
    if (ValidateAlarm(request)){
      SaveAlarm(); 
      request->send(200, "text/message", ""); 
    }
    else{
      request->send(400, "text/message", "");
      DEBUG_PRINTLN("");
      DEBUG_PRINTLN("ERROR");
      DEBUG_PRINTLN("");
    }
  });

  server.on("/alarmreset", HTTP_POST, [](AsyncWebServerRequest *request){
    ResetAlarm();
    request->send(200, "text/message", "");
  });

  server.on("/dimsave", HTTP_POST, [](AsyncWebServerRequest *request){
    if (ValidateDim(request)){
      SaveDim(); 
      request->send(200, "text/message", ""); 
    }
    else{
      request->send(400, "text/message", "");
      DEBUG_PRINTLN("");
      DEBUG_PRINTLN("ERROR");
      DEBUG_PRINTLN("");
    }
  });

  server.on("/dimreset", HTTP_POST, [](AsyncWebServerRequest *request){
    ResetDim();
    request->send(200, "text/message", "");
  });

  server.on("/globalreset", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/message", "");
    ResetColor();
    ResetNTP();
    ResetWLAN();
    ResetAlarm();
    ResetDim();
    RestartDevice();
  });

  server.on("/globalrestart", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/message", "");
    RestartDevice();
  });

  server.on("/colorsave", HTTP_POST, [](AsyncWebServerRequest *request){
    SaveColor();
    request->send(200, "text/message", "");
  });

  server.on("/colorload", HTTP_POST, [](AsyncWebServerRequest *request){
    LoadColor();
    request->send(200, "text/message", "");
  });

  server.on("/colorreset", HTTP_POST, [](AsyncWebServerRequest *request){
    ResetColor();
    request->send(200, "text/message", "");
  });
  
  server.on("/data", HTTP_POST, [](AsyncWebServerRequest *request){
    ValidateData(request);

    AsyncResponseStream *response = request->beginResponseStream("text/json");
    DynamicJsonBuffer jsonBuffer;
    JsonObject &root = jsonBuffer.createObject();
    root["brightness"] = totalBrightness;
    root["hourRed"] = hourHandColor.R;
    root["hourGreen"] = hourHandColor.G;
    root["hourBlue"] = hourHandColor.B;
    root["minuteRed"] = minuteHandColor.R;
    root["minuteGreen"] = minuteHandColor.G;
    root["minuteBlue"] = minuteHandColor.B;
    root["secondRed"] = secondHandColor.R;
    root["secondGreen"] = secondHandColor.G;
    root["secondBlue"] = secondHandColor.B;
    root["pendRed"] = pendantColor.R;
    root["pendGreen"] = pendantColor.G;
    root["pendBlue"] = pendantColor.B;
    root["minRed"] = fiveMinuteColor.R;
    root["minGreen"] = fiveMinuteColor.G;
    root["minBlue"] = fiveMinuteColor.B;
    root["quarterRed"] = quarterColor.R;
    root["quarterGreen"] = quarterColor.G;
    root["quarterBlue"] = quarterColor.B;
    root["mode"] = animationState;
    root["invert"] = invert;
    root["hour"] = timeinfo.tm_hour > 9 ? String(timeinfo.tm_hour) : "0" + String(timeinfo.tm_hour);
    root["min"] = timeinfo.tm_min > 9 ? String(timeinfo.tm_min) : "0" + String(timeinfo.tm_min);
    root["sec"] = timeinfo.tm_sec > 9 ? String(timeinfo.tm_sec) : "0" + String(timeinfo.tm_sec);
    root["NTP"] = NTPstring;
    root["GMT"] = gmtOffset_hour;
    root["DST"] = daylightOffset_hour;
    root["devicename"] = deviceName;
    root["alarmOn"] = alarmOn;
    root["minAlarm"] = minAlarm;
    root["hourAlarm"] = hourAlarm;
    root["dimOn"] = dimOn;
    root["hourStartDim"] = hourStartDim;
    root["hourStopDim"] = hourStopDim;
    root["valueDim"] = valueDim;
    root.printTo(*response);
    request->send(response);
  });
    
  server.begin();
}
//-------------------------------------------------------------------------------
/*  
 *  loop is the main loop of the device, it runs on it's own core.
 */
//-------------------------------------------------------------------------------
void loop(){
  if((WiFi.status() == WL_CONNECTED)){
    
    UDPBroadcast();
    
    if(millis() > checkTime_now + checkPeriod){
      checkTime_now = millis();
      
      CheckLocalTime();
      CheckAlarm();
      CheckAutoDim();
      
      if((timeinfo.tm_min % 5 == 0) && (timeinfo.tm_sec == 0)){
        SyncWithServer();
        DEBUG_PRINTLN("Time has been synced with server!");
      }  
    }
  }
  else{
    if(millis() > checkTime_now + checkPeriod){
      checkTime_now = millis();
      
      CheckLocalTime();
      CheckAlarm();
      CheckAutoDim();  

      if (reconnectCounter > RECONINTERVAL){
        reconnectCounter = 0;
      }
      
      if(reconnectCounter == 0){
        DEBUG_PRINTLN("No longer connected to WLAN...");

        DEBUG_PRINTLN("Trying to connect to ");
        DEBUG_PRINT(SSIDstring);
        DEBUG_PRINTLN(" using stored credentials");
    
        char cssid[SSIDstring.length()+1];
        char cpass[passwordstring.length()+1];
    
        SSIDstring.toCharArray(cssid, SSIDstring.length()+1);
        passwordstring.toCharArray(cpass, passwordstring.length()+1);
        
        WiFi.begin(cssid, cpass);
      }
      reconnectCounter++; 
    }
  }
  if(!digitalRead(0)){
    ResetWLAN();
    RestartDevice();
  }
}
//-------------------------------------------------------------------------------
/*  
 *  UDPBroadcast will send out an UTP broadcast with the device name when it hears the discovery app asking if there is "Anybody there?".
 */
//-------------------------------------------------------------------------------
void UDPBroadcast(){
  char buffer[100];
  memset(buffer, 0, 100);
  
  udp.parsePacket(); 
  
  if(udp.read(buffer, 100) > 0){
    DEBUG_PRINT("UDP data received: ");
    DEBUG_PRINTLN((char *)buffer);
    
    if(strcmp(buffer, "Anybody there?")  == 0){
      DEBUG_PRINT("UDP answer: ");  
      DEBUG_PRINT("Yes? My name is: "); 
      DEBUG_PRINTLN(deviceName);   
      udp.beginPacket(udpAddress, udpPort);
      memset(buffer, 0, 100);
      deviceName.toCharArray(buffer, 100);
      udp.printf("Yes? My name is: %s", buffer);
      udp.endPacket();
    }
  }
}
//-------------------------------------------------------------------------------
/*  
 *  RestartDevice will restart the device gracefully. It will end the task on the seperate core and will disconnect the AP when it is running.
 */
//-------------------------------------------------------------------------------
void RestartDevice(){
    vTaskDelete(LEDtask);
    delay(100);
    WiFi.softAPdisconnect();
    delay(100);
    ESP.restart();
}
//-------------------------------------------------------------------------------
void CheckLocalTime(){
  if(!getLocalTime(&timeinfo)){
    DEBUG_PRINTLN("Failed to obtain time...");
    SyncWithServer();
    return;
  }
  #ifdef DEBUG
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
  #endif
}
//-------------------------------------------------------------------------------
/*  
 *  SyncWithServer will try to sync the time with the NTP server 
 */
//-------------------------------------------------------------------------------
void SyncWithServer(void){
  char ntpServer[NTPstring.length()+1];
  NTPstring.toCharArray(ntpServer, NTPstring.length()+1);

  long gmtOffset_sec = gmtOffset_hour * 3600; 
  long daylightOffset_sec = daylightOffset_hour * 3600; 

  DEBUG_PRINTLN("Attempt to sync time with server!");
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
}
//-------------------------------------------------------------------------------
/*  
 *  ResetNTP will reset all the stored NTP data in non-volatile memory to default values.
 */
//-------------------------------------------------------------------------------
void ResetNTP(void){
  preferences.begin("NTP", false);
  preferences.clear();
  preferences.putUInt("NTPStored", 1);
  preferences.putString("NTP", NTPstringbackup);
  preferences.putLong("GMT", 0);
  preferences.putInt("DST", 0);
  preferences.end();
  
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("NTP settings reset...");
  DEBUG_PRINTLN("");

  LoadNTP();
}
//-------------------------------------------------------------------------------
/*  
 *  LoadNTP will load stored NTP data from non-volatile memory.
 */
//-------------------------------------------------------------------------------
void LoadNTP(void){
  preferences.begin("NTP", false);
  NTPstring = preferences.getString("NTP");
  gmtOffset_hour = preferences.getFloat("GMT",0);
  daylightOffset_hour = preferences.getFloat("DST",0);
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("NTP settings loaded...");
  DEBUG_PRINT("NTP server is: ");
  DEBUG_PRINTLN(NTPstring);
  DEBUG_PRINT("GMT is: ");
  DEBUG_PRINTLN(gmtOffset_hour);
  DEBUG_PRINT("DST is: ");
  DEBUG_PRINTLN(daylightOffset_hour);
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  SaveNTP will save NTP data to non-volatile memory.
 */
//-------------------------------------------------------------------------------
void SaveNTP(void){
  preferences.begin("NTP", false);
  preferences.putUInt("NTPStored", 1);
  preferences.putString("NTP", NTPstring);
  preferences.putFloat("GMT", gmtOffset_hour);
  preferences.putFloat("DST", daylightOffset_hour);
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("NTP settings loaded...");
  DEBUG_PRINT("NTP server is: ");
  DEBUG_PRINTLN(NTPstring);
  DEBUG_PRINT("GMT is: ");
  DEBUG_PRINTLN(gmtOffset_hour);
  DEBUG_PRINT("DST is: ");
  DEBUG_PRINTLN(daylightOffset_hour);
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  ResetColor will reset all the stored Color data in non-volatile memory to default values.
 */
//-------------------------------------------------------------------------------
void ResetColor(void){
  preferences.begin("color", false);
  preferences.clear();
  preferences.putUInt("colorStored", 0);
  preferences.putUInt("brightness", 255);
  preferences.putUInt("hourRed", 255);
  preferences.putUInt("hourGreen", 0);
  preferences.putUInt("hourBlue", 0);
  preferences.putUInt("minuteRed", 0);
  preferences.putUInt("minuteGreen", 255);
  preferences.putUInt("minuteBlue", 0);
  preferences.putUInt("secondRed", 0);
  preferences.putUInt("secondGreen", 0);
  preferences.putUInt("secondBlue", 255);
  preferences.putUInt("pendRed", 0);
  preferences.putUInt("pendGreen", 0);
  preferences.putUInt("pendBlue", 0);
  preferences.putUInt("minRed", 0);
  preferences.putUInt("minGreen", 0);
  preferences.putUInt("minBlue", 0);
  preferences.putUInt("quarterRed", 0);
  preferences.putUInt("quarterGreen", 0);
  preferences.putUInt("quarterBlue", 0);
  preferences.putUInt("mode", 0);
  preferences.putUInt("invert", 0);
  LoadColor();
  preferences.end();
  
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Color settings cleared...");
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  LoadColor will load stored Color data from non-volatile memory.
 */
//-------------------------------------------------------------------------------
void LoadColor(void){
  preferences.begin("color", false);
  totalBrightness = preferences.getUInt("brightness", 255);
  hourHandColor.R = preferences.getUInt("hourRed", 255);
  hourHandColor.G = preferences.getUInt("hourGreen", 0);
  hourHandColor.B = preferences.getUInt("hourBlue", 0);
  minuteHandColor.R = preferences.getUInt("minuteRed", 0);
  minuteHandColor.G = preferences.getUInt("minuteGreen", 255);
  minuteHandColor.B = preferences.getUInt("minuteBlue", 0);
  secondHandColor.R = preferences.getUInt("secondRed", 0);
  secondHandColor.G = preferences.getUInt("secondGreen", 0);
  secondHandColor.B = preferences.getUInt("secondBlue", 255);
  pendantColor.R = preferences.getUInt("pendRed", 0);
  pendantColor.G = preferences.getUInt("pendGreen", 0);
  pendantColor.B = preferences.getUInt("pendBlue", 0);
  fiveMinuteColor.R = preferences.getUInt("minRed", 0);
  fiveMinuteColor.G = preferences.getUInt("minGreen", 0);
  fiveMinuteColor.B = preferences.getUInt("minBlue", 0);
  quarterColor.R = preferences.getUInt("quarterRed", 0);
  quarterColor.G = preferences.getUInt("quarterGreen", 0);
  quarterColor.B = preferences.getUInt("quarterBlue", 0);
  animationState = preferences.getUInt("mode", 0);
  invert = preferences.getUInt("invert", 0);
  
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Color settings loaded...");
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  SaveColor will save Color data to non-volatile memory.
 */
//-------------------------------------------------------------------------------
void SaveColor(void){
  preferences.begin("color", false);
  preferences.putUInt("colorStored", 1);
  preferences.putUInt("brightness", totalBrightness);
  preferences.putUInt("hourRed", hourHandColor.R);
  preferences.putUInt("hourGreen", hourHandColor.G);
  preferences.putUInt("hourBlue", hourHandColor.B);
  preferences.putUInt("minuteRed", minuteHandColor.R);
  preferences.putUInt("minuteGreen", minuteHandColor.G);
  preferences.putUInt("minuteBlue", minuteHandColor.B);
  preferences.putUInt("secondRed", secondHandColor.R);
  preferences.putUInt("secondGreen", secondHandColor.G);
  preferences.putUInt("secondBlue", secondHandColor.B);
  preferences.putUInt("pendRed", pendantColor.R);
  preferences.putUInt("pendGreen", pendantColor.G);
  preferences.putUInt("pendBlue", pendantColor.B);
  preferences.putUInt("minRed", fiveMinuteColor.R);
  preferences.putUInt("minGreen", fiveMinuteColor.G);
  preferences.putUInt("minBlue", fiveMinuteColor.B);
  preferences.putUInt("quarterRed", quarterColor.R);
  preferences.putUInt("quarterGreen", quarterColor.G);
  preferences.putUInt("quarterBlue", quarterColor.B);
  preferences.putUInt("mode", animationState);
  preferences.putUInt("invert", invert);
  preferences.end();
  
  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Color settings stored...");
  DEBUG_PRINTLN("");
  
}
//-------------------------------------------------------------------------------
/*  
 *  ResetWLAN will reset all the stored WLAN data in non-volatile memory to default values.
 */
//-------------------------------------------------------------------------------
void ResetWLAN(void){
  preferences.begin("WLAN", false);
  preferences.clear();
  preferences.end();
  
  deviceName = preferences.getString("name","K2400");
  WLANOn = false;

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("WLAN settings cleared...");
  DEBUG_PRINT("Device name is: ");
  DEBUG_PRINTLN(deviceName);
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  LoadWLAN will load stored WLAN data from non-volatile memory.
 */
//-------------------------------------------------------------------------------
void LoadWLAN(void){

  preferences.begin("WLAN", false);
  WLANOn = preferences.getBool("WLANStored", 0);
  SSIDstring = preferences.getString("SSID","");
  passwordstring = preferences.getString("password","");
  deviceName = preferences.getString("name","K2400");
  
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("WLAN settings loaded...");
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  SaveWLAN will save WLAN data to non-volatile memory.
 */
//-------------------------------------------------------------------------------
void SaveWLAN(void){
  WLANOn = true;
  
  preferences.begin("WLAN", false);
  preferences.putBool("WLANStored", WLANOn);
  preferences.putString("SSID", SSIDstring);
  preferences.putString("password", passwordstring);
  preferences.putString("name", deviceName);
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("WLAN settings stored...");
  DEBUG_PRINT("Device name is: ");
  DEBUG_PRINTLN(deviceName);
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  ResetDim will reset all the stored Auto Dim data in non-volatile memory to default values.
 */
//-------------------------------------------------------------------------------
void ResetDim(void){
  dimOn = 0;
  hourStartDim = 0;
  hourStopDim = 0;
  valueDim = 0;
  
  preferences.begin("dim", false);
  preferences.clear();
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("AUTO DIM settings cleared...");

  SaveDim();
}
//-------------------------------------------------------------------------------
/*  
 *  LoadDim will load stored Auto Dim data from non-volatile memory.
 */
//-------------------------------------------------------------------------------
void LoadDim(void){

  preferences.begin("dim", false);
  dimOn = preferences.getUInt("dimOn",0);
  hourStartDim = preferences.getUInt("hourstart",0);
  hourStopDim = preferences.getUInt("hourstop",0);
  valueDim = preferences.getUInt("value",0);
  
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Auto Dim settings loaded...");
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  SaveDim will save Auto Dim data to non-volatile memory.
 */
//-------------------------------------------------------------------------------
void SaveDim(void){
  
  preferences.begin("dim", false);
  preferences.putBool("dimStored", 1);
  preferences.putUInt("dimOn", dimOn);
  preferences.putUInt("hourstart", hourStartDim);
  preferences.putUInt("hourstop", hourStopDim);
  preferences.putUInt("value", valueDim);
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("AUTO DIM settings stored...");
  if (dimOn){
    DEBUG_PRINTLN("AUTO DIM is ON");
    DEBUG_PRINT("AUTO DIM is set for: ");
  }
  if (!dimOn){
    DEBUG_PRINTLN("AUTO DIM is set OFF");
    DEBUG_PRINT("AUTO DIM is for (but will not go off): ");
  }
  DEBUG_PRINT("Start: ");
  DEBUG_PRINT(hourStartDim);
  DEBUG_PRINT(" stop: ");
  DEBUG_PRINT(hourStopDim);
  DEBUG_PRINT(" value: ");
  DEBUG_PRINTLN(valueDim);
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  ResetAlarm will reset all the stored Alarm data in non-volatile memory to default values.
 */
//-------------------------------------------------------------------------------
void ResetAlarm(void){
  alarmOn = 0;
  hourAlarm = 0;
  minAlarm = 0;
  
  preferences.begin("alarm", false);
  preferences.clear();
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("ALARM settings cleared...");

  SaveAlarm();
}
//-------------------------------------------------------------------------------
/*  
 *  LoadAlarm will load stored Alarm data from non-volatile memory.
 */
//-------------------------------------------------------------------------------
void LoadAlarm(void){

  preferences.begin("alarm", false);
  alarmOn = preferences.getUInt("alarmOn",0);
  hourAlarm = preferences.getUInt("hour",0);
  minAlarm = preferences.getUInt("min",0);
  
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Alarm settings loaded...");
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  SaveAlarm will save Alarm data to non-volatile memory.
 */
//-------------------------------------------------------------------------------
void SaveAlarm(void){
  
  preferences.begin("alarm", false);
  preferences.putBool("alarmStored", 1);
  preferences.putUInt("alarmOn", alarmOn);
  preferences.putUInt("hour", hourAlarm);
  preferences.putUInt("min", minAlarm);
  preferences.end();

  DEBUG_PRINTLN("");
  DEBUG_PRINTLN("Alarm settings stored...");
  if (alarmOn){
    DEBUG_PRINTLN("Alarm is ON");
    DEBUG_PRINT("Alarm is set for: ");
  }
  if (!alarmOn){
    DEBUG_PRINTLN("Alarm is OFF");
    DEBUG_PRINT("Alarm is set for (but will not go off): ");
  }
  DEBUG_PRINT(hourAlarm);
  DEBUG_PRINT(":");
  DEBUG_PRINTLN(minAlarm);
  DEBUG_PRINTLN("");
}
//-------------------------------------------------------------------------------
/*  
 *  ClearArrays will clear all animation arrays.
 */
//-------------------------------------------------------------------------------
void ClearArrays(void){
  RgbColor color = (0,0,0);
  for (int i=0; i < (PixelCount); i++){
    animationArray1[i] = color;
    animationArray2[i] = color;
    animationArray3[i] = color;
    animationArray4[i] = color;
    animationArray5[i] = color;
    animationArray6[i] = color;
    animationArray7[i] = color;
    strip.SetPixelColor(i, color);
  }
}
//-------------------------------------------------------------------------------
/*  
 *  BlendArrays will blend all used animation arrays into 1. It will also take into account all brightness values.
 */
//-------------------------------------------------------------------------------
void BlendArrays(void){
  RgbColor color;
  int blendBrightness = map(totalBrightness, 0, 255, 0, maxBrightness);
  blendBrightness = map(blendBrightness, 0, 255, 0, dimBrightness);
  
  for (int i=0; i < (PixelCount); i++){

    color = AddColors(RgbColor(0,0,0),animationArray1[i]);
    color = AddColors(color,animationArray2[i]);
    color = AddColors(color,animationArray3[i]);
    color = AddColors(color,animationArray4[i]);
    color = AddColors(color,animationArray5[i]);
    color = AddColors(color,animationArray6[i]);
    color = AddColors(color,animationArray7[i]);

    if (invert ==1){
      color.R = 255-color.R;
      color.G = 255-color.G;
      color.B = 255-color.B;
    }
    
    color = colorGamma.Correct(color);

    color = AdjustBrightness(color, blendBrightness);

    strip.SetPixelColor(i, color);
  }
}
//-------------------------------------------------------------------------------
/*  
 *  AdjustBrightness will change the brightness value of an RgbColor (8bit brightness)
 */
//-------------------------------------------------------------------------------
RgbColor AdjustBrightness(RgbColor color, int brightness){

  color.R = map(color.R, 0, 255, 0, brightness);
  color.G = map(color.G, 0, 255, 0, brightness);
  color.B = map(color.B, 0, 255, 0, brightness);

  return color;
}
//-------------------------------------------------------------------------------
/*  
 *  AddColors will add two RgbColor objects together without wrapping (capping at 255).
 */
//-------------------------------------------------------------------------------
RgbColor AddColors(RgbColor color1, RgbColor color2){
  int _newR = color1.R + color2.R;
  int _newG = color1.G + color2.G;
  int _newB = color1.B + color2.B;
  
  uint8_t newR = (_newR > 255) ? 255 : _newR;
  uint8_t newG = (_newG > 255) ? 255 : _newG;
  uint8_t newB = (_newB > 255) ? 255 : _newB;
  
  return RgbColor(newR, newG, newB);
}
//-------------------------------------------------------------------------------
/*  
 *  ProcessTime will make sure that the correct time is in the global time vars when called.
 */
//-------------------------------------------------------------------------------
void ProcessTime(void ){

  hourCalc = HourCalc();
  minCalc = timeinfo.tm_min;
  secCalc = timeinfo.tm_sec;
  
}
//-------------------------------------------------------------------------------
/*  
 *  HourCalc will calculate the hour indication on a 60 minute scale.
 */
//-------------------------------------------------------------------------------
int HourCalc(void ){
  int hourTemp;
  int minTemp;
  
  hourTemp = timeinfo.tm_hour;
  hourDirectCalc = hourTemp;
  minTemp = timeinfo.tm_min;

  if (hourTemp < 12){
    hourTemp  = hourTemp * 5;
  }
  else{
    hourTemp  = hourTemp - 12;
    hourTemp  = hourTemp * 5;
  }
  
  minTemp = minTemp / 12;
  hourTemp = hourTemp + minTemp;

  return hourTemp;
}
//-------------------------------------------------------------------------------
void AlarmAnimation(const AnimationParam& param){
  float progress = animation1(param.progress);
  
  for (int i=0; i < (PixelCount); i++){
    animationArray7[i] = RgbColor (0,0,0);
  }

  for (int i=0; i < (PixelCount); i++){
    animationArray7[i] = RgbColor::LinearBlend(RgbColor(255,0,0), RgbColor(0,0,0), progress);
  } 

  if (param.state == AnimationState_Completed)
  {
    if (durationAlarm > 0){
      durationAlarm--;
      animations.RestartAnimation(param.index);
    }
    else if (durationAlarm == 0){
      //do nothing;
    }
    
  }
}
//-------------------------------------------------------------------------------
void HourHand0(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray5[i] = RgbColor (0,0,0);
  }

  animationArray5[hourCalc] = hourHandColor;
  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void HourHand1(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray5[i] = RgbColor (0,0,0);
  }

  animationArray5[hourCalc] = hourHandColor;

  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void HourHand2(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray5[i] = RgbColor (0,0,0);
  }

  float fade = (float)minCalc / 59.0;

  animationArray5[hourCalc] = RgbColor::LinearBlend(hourHandColor, RgbColor(0,0,0), fade);
  animationArray5[circleArray[hourCalc+1]] = RgbColor::LinearBlend(RgbColor(0,0,0), hourHandColor, fade);


  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void HourHand3(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray5[i] = RgbColor (0,0,0);
  }

  for (int j=0; j <= (hourCalc); j++){
    animationArray5[j] = hourHandColor;
  }
  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
 void HourHand4(const AnimationParam& param){
  float progress = animation4(param.progress);
  float fade = (float)minCalc/59.0;

  for (int j=0; j < (hourCalc); j++){
    if((animationArray5[j].R != hourHandColor.R)||(animationArray5[j].G != hourHandColor.G)||(animationArray5[j].B != hourHandColor.B)){
      animationArray5[j] = RgbColor::LinearBlend(animationArray5[j], hourHandColor, progress);
    }
    else{
      animationArray5[j] = hourHandColor;
    } 
  }
  
  animationArray5[hourCalc] = RgbColor::LinearBlend(RgbColor(0,0,0), hourHandColor, fade); 

  for (int i = hourCalc+1; i < (PixelCount); i++){
    animationArray5[i] = RgbColor(0,0,0);
  }

  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void HourHand5(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray5[i] = RgbColor (0,0,0);
  }

  for (int j=(hourCalc); j < (PixelCount); j++){
    animationArray5[j] = hourHandColor;
  }
  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void HourHand6(const AnimationParam& param){
  float progress = animation4(param.progress);
  float fade = (float)minCalc/59.0;
 
  for (int j=(hourCalc); j < (PixelCount); j++){
    if((animationArray5[j].R != hourHandColor.R)||(animationArray5[j].G != hourHandColor.G)||(animationArray5[j].B != hourHandColor.B)){
      animationArray5[j] = RgbColor::LinearBlend(animationArray5[j], hourHandColor, progress);
    }
    else{
      animationArray5[j] = hourHandColor;
    } 
  }
  
  animationArray5[hourCalc] = RgbColor::LinearBlend(hourHandColor, RgbColor(0,0,0), fade); 
  
  for (int i=0; i < (hourCalc); i++){
    animationArray5[i] = RgbColor(0,0,0);
  }
  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void HourHand7(const AnimationParam& param){
  float progress = animation5(param.progress);
  
  uint16_t nextPixel;
  
  nextPixel = progress * hourCalc;
  
  animationArray5[circleArray[nextPixel]] = hourHandColor;
    
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void HourHand8(const AnimationParam& param){
  float progress = animation5(param.progress);
  
  uint16_t nextPixel;
  
  nextPixel = progress * 60;

  nextPixel = nextPixel + hourCalc;

  if(nextPixel > 60){
    nextPixel = nextPixel - 60;
  }

  if(nextPixel > 60){
    nextPixel = 60;
  }
  
  animationArray5[circleArray[nextPixel]] = hourHandColor;
    
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void MinuteHand0(const AnimationParam& param){
  int minTemp;

  RgbColor minuteHandColor1 = RgbColor::LinearBlend(minuteHandColor, RgbColor(0,0,0), 0.3f);
  RgbColor minuteHandColor2 = RgbColor::LinearBlend(minuteHandColor, RgbColor(0,0,0), 0.6f);
  RgbColor minuteHandColor3 = RgbColor::LinearBlend(minuteHandColor, RgbColor(0,0,0), 0.75f);
  
  for (int i=0; i < (PixelCount); i++){
    animationArray4[i] = RgbColor (0,0,0);
  }
  
  minTemp = minCalc;
  
  animationArray4[minCalc] = minuteHandColor;

  if(minCalc == 0){
    minTemp = 60;
    animationArray4[(minTemp - 1)] = minuteHandColor1;
    animationArray4[(minTemp - 2)] = minuteHandColor2;
    animationArray4[(minTemp - 3)] = minuteHandColor3;
  }
  else if(minCalc == 1){
    animationArray4[(minTemp - 1)] = minuteHandColor1;
    minTemp = 61;
    animationArray4[(minTemp - 2)] = minuteHandColor2;
    animationArray4[(minTemp - 3)] = minuteHandColor3;
  }
  else if(minCalc == 2){
    animationArray4[(minTemp - 1)] = minuteHandColor1;
    animationArray4[(minTemp - 2)] = minuteHandColor2;
    minTemp = 62;
    animationArray4[(minTemp - 3)] = minuteHandColor3;
  }
  else{
    animationArray4[(minTemp - 1)] = minuteHandColor1;
    animationArray4[(minTemp - 2)] = minuteHandColor2;
    animationArray4[(minTemp - 3)] = minuteHandColor3;
  }

  if (param.state == AnimationState_Completed)
    {
      animations.RestartAnimation(param.index);
    }
}
//-------------------------------------------------------------------------------
void MinuteHand1(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray4[i] = RgbColor (0,0,0);
  }
  
  animationArray4[minCalc] = minuteHandColor;

  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void MinuteHand2(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray4[i] = RgbColor (0,0,0);
  }

  float fade = (float)secCalc / 59.0;

  animationArray4[minCalc] = RgbColor::LinearBlend(minuteHandColor, RgbColor(0,0,0), fade);
  animationArray4[circleArray[minCalc+1]] = RgbColor::LinearBlend(RgbColor(0,0,0), minuteHandColor, fade);


  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void MinuteHand3(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray4[i] = RgbColor (0,0,0);
  }

  for (int j=0; j <= (minCalc); j++){
    animationArray4[j] = minuteHandColor;
  }
  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void MinuteHand4(const AnimationParam& param){
  float progress = animation4(param.progress);
  float fade = (float)secCalc/59.0;

  for (int j=0; j < (minCalc); j++){
    if((animationArray4[j].R != minuteHandColor.R)||(animationArray4[j].G != minuteHandColor.G)||(animationArray4[j].B != minuteHandColor.B)){
      animationArray4[j] = RgbColor::LinearBlend(animationArray4[j], minuteHandColor, progress);
    }
    else{
      animationArray4[j] = minuteHandColor;
    } 
  }
  
  animationArray4[minCalc] = RgbColor::LinearBlend(RgbColor(0,0,0), minuteHandColor, fade); 

  for (int i= minCalc+1; i < (PixelCount); i++){
    animationArray4[i] = RgbColor(0,0,0);
  }
  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void MinuteHand5(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray4[i] = RgbColor (0,0,0);
  }

  for (int j=(minCalc); j < (PixelCount); j++){
    animationArray4[j] = minuteHandColor;
  }
  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void MinuteHand6(const AnimationParam& param){
  float progress = animation4(param.progress);
  float fade = (float)secCalc/59.0;
 
  for (int j=(minCalc); j < (PixelCount); j++){
    if((animationArray4[j].R != minuteHandColor.R)||(animationArray4[j].G != minuteHandColor.G)||(animationArray4[j].B != minuteHandColor.B)){
      animationArray4[j] = RgbColor::LinearBlend(animationArray4[j], minuteHandColor, progress);
    }
    else{
      animationArray4[j] = minuteHandColor;
    } 
  }
  
  animationArray4[minCalc] = RgbColor::LinearBlend(minuteHandColor, RgbColor(0,0,0), fade); 
  
  for (int i=0; i < (minCalc); i++){
    animationArray4[i] = RgbColor(0,0,0);
  }
  
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void MinuteHand7(const AnimationParam& param){
  float progress = animation5(param.progress);
  
  uint16_t nextPixel;
  
  nextPixel = progress * minCalc;
  
  animationArray4[circleArray[nextPixel]] = minuteHandColor;
    
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void MinuteHand8(const AnimationParam& param){
  float progress = animation5(param.progress);
  
  uint16_t nextPixel;
  
  nextPixel = progress * 60;

  nextPixel = nextPixel + minCalc;

  if(nextPixel > 60){
    nextPixel = nextPixel - 60;
  }

  if(nextPixel > 60){
    nextPixel = 60;
  }
  
  animationArray4[circleArray[nextPixel]] = minuteHandColor;
    
  if (param.state == AnimationState_Completed)
  {
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void SecondHand0(const AnimationParam& param){
  animationArray3[timeinfo.tm_sec] = secondHandColor;

  if (param.state == AnimationState_Completed)
    {
      ProcessTime();
      animations.RestartAnimation(param.index);
    }
}
//-------------------------------------------------------------------------------
void SecondHand1(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray3[i] = RgbColor (0,0,0);
  }
  
  animationArray3[timeinfo.tm_sec] = secondHandColor;
  
  if (param.state == AnimationState_Completed)
  {
    ProcessTime();
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void SecondHand2(const AnimationParam& param){
  float progress = animation4(param.progress);
  for (int i=0; i < (PixelCount); i++){
    animationArray3[i] = RgbColor (0,0,0);
  }
   
  animationArray3[secCalc] = RgbColor::LinearBlend(secondHandColor, RgbColor(0,0,0), progress);
  animationArray3[circleArray[secCalc+1]] = RgbColor::LinearBlend(RgbColor(0,0,0), secondHandColor, progress);
  
  if (param.state == AnimationState_Completed)
  {
    ProcessTime();
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void SecondHand3(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray3[i] = RgbColor (0,0,0);
  }
  
  for (int j=0; j <= (secCalc); j++){
    animationArray3[j] = secondHandColor;
  }
  
  if (param.state == AnimationState_Completed)
  {
    ProcessTime();
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void SecondHand4(const AnimationParam& param){
  float progress = animation4(param.progress);
 
  for (int j=0; j < (secCalc); j++){
    if((animationArray3[j].R != secondHandColor.R)||(animationArray3[j].G != secondHandColor.G)||(animationArray3[j].B != secondHandColor.B)){
      animationArray3[j] = RgbColor::LinearBlend(animationArray3[j], secondHandColor, progress);
    }
    else{
      animationArray3[j] = secondHandColor;
    } 
  }
  
  animationArray3[secCalc] = RgbColor::LinearBlend(RgbColor(0,0,0), secondHandColor, progress); 
  
  for (int i= secCalc+1; i < (PixelCount); i++){
    animationArray3[i] = RgbColor(0,0,0);
  }
  
  if (param.state == AnimationState_Completed)
  {
    ProcessTime();
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void SecondHand5(const AnimationParam& param){
  for (int i=0; i < (PixelCount); i++){
    animationArray3[i] = RgbColor (0,0,0);
  }

  for (int j=(timeinfo.tm_sec); j < (PixelCount); j++){
    animationArray3[j] = secondHandColor;
  }
  
  if (param.state == AnimationState_Completed)
  {
    ProcessTime();
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void SecondHand6(const AnimationParam& param){
  float progress = animation4(param.progress);
 
  for (int j=(secCalc); j < (PixelCount); j++){
    if((animationArray3[j].R != secondHandColor.R)||(animationArray3[j].G != secondHandColor.G)||(animationArray3[j].B != secondHandColor.B)){
      animationArray3[j] = RgbColor::LinearBlend(animationArray3[j], secondHandColor, progress);
    }
    else{
      animationArray3[j] = secondHandColor;
    } 
  }
  
  animationArray3[secCalc] = RgbColor::LinearBlend(secondHandColor, RgbColor(0,0,0), progress); 
  
  for (int i=0; i < (secCalc); i++){
    animationArray3[i] = RgbColor(0,0,0);
  }
  
  if (param.state == AnimationState_Completed)
  {
    ProcessTime();
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void SecondHand7(const AnimationParam& param){
  float progress = animation5(param.progress);
  
  uint16_t nextPixel;
  
  nextPixel = progress * secCalc;
  
  animationArray3[circleArray[nextPixel]] = secondHandColor;
    
  if (param.state == AnimationState_Completed)
  {
    ProcessTime();
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void SecondHand8(const AnimationParam& param){
  float progress = animation5(param.progress);
  
  uint16_t nextPixel;
  
  nextPixel = progress * 60;

  nextPixel = nextPixel + secCalc;

  if(nextPixel > 60){
    nextPixel = nextPixel - 60;
  }

  if(nextPixel > 60){
    nextPixel = 60;
  }
  
  animationArray3[circleArray[nextPixel]] = secondHandColor;
    
  if (param.state == AnimationState_Completed)
  {
    ProcessTime();
    animations.RestartAnimation(param.index);
  }
}
//-------------------------------------------------------------------------------
void AllTails(const AnimationParam& param){
  if (param.state == AnimationState_Completed)
    {
      RgbColor color;
        for (uint16_t indexPixel = 0; indexPixel <= 59; indexPixel++)
        {
            color = animationArray3[indexPixel];
            color.Darken(2);
            animationArray3[indexPixel] = color;
            color = animationArray4[indexPixel];
            color.Darken(2);
            animationArray4[indexPixel] = color;
            color = animationArray5[indexPixel];
            color.Darken(2);
            animationArray5[indexPixel] = color;
        }
            animations.RestartAnimation(param.index);
    }
}
//-------------------------------------------------------------------------------
void SecondHand0Tail(const AnimationParam& param){
  if (param.state == AnimationState_Completed)
    {
      RgbColor color;
        for (uint16_t indexPixel = 0; indexPixel <= 59; indexPixel++)
        {
            color = animationArray3[circleArray[indexPixel]];
            
            color.Darken(secondHandTail);

            animationArray3[indexPixel] = color;
            
        }
            animations.RestartAnimation(param.index);
    }
}
//-------------------------------------------------------------------------------
void QuarterMarks(const AnimationParam& param){
  float progress = animation3(param.progress);
  /*RgbColor quarterColor;
  
  if(fadeDir2){
    quarterColor = RgbColor::LinearBlend(quarterColor2, quarterColor1, progress);
  }
  else
  {
    quarterColor = RgbColor::LinearBlend(quarterColor1, quarterColor2, progress);
  }*/

  for (int i=0; i < (PixelCount/15); i++){   
    animationArray6[quarterNumbers[i]] = quarterColor;
  }

  if (param.state == AnimationState_Completed)
    {
      animations.RestartAnimation(param.index);
      //fadeDir2 = !fadeDir2;  
    }
}
//-------------------------------------------------------------------------------
void FiveMinuteMarks(const AnimationParam& param){
  float progress = animation2(param.progress);
  /*RgbColor fiveMinuteColor;
  
  if(fadeDir){
    fiveMinuteColor = RgbColor::LinearBlend(fiveMinuteColor2, fiveMinuteColor1, progress);
  }
  else
  {
    fiveMinuteColor = RgbColor::LinearBlend(fiveMinuteColor1, fiveMinuteColor2, progress);
  }*/

  for (int i=0; i < (PixelCount/5); i++){   
    animationArray2[fiveMinuteNumbers[i]] = fiveMinuteColor;
  }

  if (param.state == AnimationState_Completed)
    {
      animations.RestartAnimation(param.index);
      //fadeDir = !fadeDir;  
    }
}
//-------------------------------------------------------------------------------
void Pendant(const AnimationParam& param){
  float progress = animation1(param.progress);

  uint16_t nextPixel;
  
  if (pendantDir){
    nextPixel = progress * 60;
    
  }
  else{
    nextPixel = progress * 60;
    nextPixel = 60 - nextPixel;
  }

  animationArray1[circleArray[nextPixel]] = pendantColor;
    
  if (param.state == AnimationState_Completed)
    {
      animations.RestartAnimation(param.index);
      pendantDir = !pendantDir;   
    }
}
//-------------------------------------------------------------------------------
void Pendanttail(const AnimationParam& param){
  if (param.state == AnimationState_Completed)
    {
      RgbColor color;
        for (uint16_t indexPixel = 0; indexPixel <= 59; indexPixel++)
        {
            color = animationArray1[circleArray[indexPixel]];
            
            color.Darken(pendantTail);

            animationArray1[indexPixel] = color;
            
        }
            animations.RestartAnimation(param.index);
    }
}
