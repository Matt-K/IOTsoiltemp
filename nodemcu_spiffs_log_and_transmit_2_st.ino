//index file format: 
//index,internal time (unix),trigger time (unix)
//
//data file format:
//internal time,temperature,battery voltage
//internal time,temperature,battery voltage
//internal time,temperature,battery voltage
//...

#include "FS.h"
#include "ThingSpeak.h"
#include "secrets.h"
#include <ESP8266WiFi.h>

#include <NTPClient.h>
#include <WiFiUdp.h>

#include <OneWire.h>
#include <DallasTemperature.h>

// Data wire is plugged into port 12 on the Arduino
#define ONE_WIRE_BUS 12
// Setup a oneWire instance to communicate with any OneWire devices (not just Maxim/Dallas temperature ICs)
OneWire oneWire(ONE_WIRE_BUS);
// Pass our oneWire reference to Dallas Temperature. 
DallasTemperature sensors(&oneWire);

char ssid[] = SECRET_SSID;   // your network SSID (name) 
char pass[] = SECRET_PASS;   // your network password
int keyIndex = 0;            // your network key Index number (needed only for WEP)
WiFiClient  client;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP);

File f;
unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;
float tempF = 0.0;
float battvolt = 0.0;
float filetempF = 0.0;
float filebattvolt = 0.0;
unsigned long internaltime=0;
unsigned long fileinternaltime=0;
int dindex=0;
unsigned long triggertime=0;
char delim=',';
int logints=60*15;  //logging interval in seconds-------------------------
int uploadintmin=60*12; //upload interval, in minutes---------------------------
String line;

void setup() {
SPIFFS.begin();
Serial.begin(115200);  // Initialize serial
Serial.println("");
//WiFi.setSleepMode(WIFI_LIGHT_SLEEP);
//delay(10);
WiFi.mode(WIFI_STA);
//open index file
//read index
//if index file is empty, initialize files and collect data
f = SPIFFS.open("/index.txt", "r");
if ((!f)||(f.size()<2)){
  Serial.println("index file is empty or missing, initializing");
  f.close();
  //clear index file
  f = SPIFFS.open("/index.txt", "w");
  f.close();
  Serial.println("index file created");
  //clear data file
  f = SPIFFS.open("/data.txt", "w");
  f.close();  
  Serial.println("data file created");
  dindex=0;
  Serial.println("index reset to 0");
  updatetime();
  triggertime=internaltime+(uploadintmin*60);
  recorddata();
}
else {
  //read internal time
  //read trigger time
  //close index file
  line=f.readStringUntil(delim);
  dindex=line.toInt();
  Serial.print("last index is ");
  Serial.println(dindex); 
  line=f.readStringUntil(delim);
  internaltime=line.toInt();
  Serial.print("last internal time is ");
  Serial.println(internaltime);
  line=f.readStringUntil('\n');
  triggertime=line.toInt();
  Serial.print("trigger time is ");
  Serial.println(triggertime);
  f.close();

  //increment internal time
  internaltime=internaltime+logints;
  Serial.print("new internal time is ");
  Serial.println(internaltime);

  //if internal time < transmit trigger
  //record new data
  //if internal time >= transmit trigger
  //transmit data
  if (internaltime<triggertime){
    recorddata();
  }
  else {
    transmitdata();
  }
}
}

void recorddata(){
Serial.println("recording new data");
//read sensors
sensors.requestTemperatures(); // Send the command to get temperatures
tempF = sensors.getTempFByIndex(0);
//get battery voltage
battvolt=float(analogRead(A0))/1024.0*3.3;

//open data file
f = SPIFFS.open("/data.txt", "a");
//write new data line (internal time, temperature, battery voltage)
f.print(internaltime);
f.print(delim);
f.print(tempF);
f.print(delim);
f.println(battvolt);
//close data file
f.close();
Serial.println("data file updated");
//increment index
dindex=dindex+1;
//open index file
f = SPIFFS.open("/index.txt", "w");
//write new index to index file
//write new internal time to index file
//write trigger time to index file
f.print(dindex);
f.print(delim);
f.print(internaltime);
f.print(delim);
f.println(triggertime);
//close index file
f.close();
Serial.println("index file updated");
//go to sleep
Serial.println("Going to sleep");
Serial.println("");
 ESP.deepSleep(logints*(1e6)); //deep sleep for [seconds]e6 (900 is 15 minutes, 3600 is 1 hour)
}

void transmitdata(){
Serial.println("transmitting data to thingspeak");
//transmit data:
//connect to wifi
WiFi.mode(WIFI_STA); 
ThingSpeak.begin(client);  // Initialize ThingSpeak
// Connect or reconnect to WiFi
if(WiFi.status() != WL_CONNECTED){
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(SECRET_SSID);
  while(WiFi.status() != WL_CONNECTED){
    WiFi.begin(ssid, pass);  // Connect to WPA/WPA2 network. Change this line if using open or WEP network
    Serial.print(".");
    delay(5000);     
  } 
  Serial.println("\nConnected.");
}

//open data file at beginning to read
f = SPIFFS.open("/data.txt", "r");
//for count=1 to index
//  read a line of data
//  parse data

for (int count=1;count<=dindex;count++){
  Serial.print("transmitting data line ");
  Serial.println(count);
  line=f.readStringUntil(delim);
  fileinternaltime=line.toInt();
  line=f.readStringUntil(delim);
  filetempF=line.toFloat();
  line=f.readStringUntil('\n');
  filebattvolt=line.toFloat();

  //  transmit data
  ThingSpeak.setField(1, String(fileinternaltime));
  ThingSpeak.setField(2, filetempF);
  ThingSpeak.setField(3, filebattvolt);
  int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
  if(x == 200){
    Serial.print("line ");
    Serial.print(count);
    Serial.println(" transmitted successfully");
  }
  else{
    Serial.println("Problem updating channel. HTTP error code " + String(x));
  }
  delay(15500);
}
f.close();
//update internal time from network time
updatetime();

//clear data file
Serial.println("clearing data file");
f = SPIFFS.open("/data.txt", "w");
f.close();

//generate new transmit trigger time
Serial.println("generating new transmit trigger time");
triggertime=internaltime+(uploadintmin*60);
//zero index
Serial.println("zeroing index");
dindex=0;
//open index file
Serial.println("updating index file");
f = SPIFFS.open("/index.txt", "w");
//write new (0) index
//write internal time
//write new transmit trigger time
f.print(dindex);
f.print(delim);
f.print(internaltime);
f.print(delim);
f.println(triggertime);
//close index file
f.close();
//record new data (includes go to sleep)
recorddata();
}

void updatetime(){
Serial.println("updating interal time from network time");
//update internal time from network time:
//connect to wifi if not already connected
WiFi.mode(WIFI_STA);
 // Connect or reconnect to WiFi
if(WiFi.status() != WL_CONNECTED){
  Serial.print("Attempting to connect to SSID: ");
  Serial.println(SECRET_SSID);
  while(WiFi.status() != WL_CONNECTED){
    WiFi.begin(ssid, pass);  // Connect to WPA/WPA2 network. Change this line if using open or WEP network
    Serial.print(".");
    delay(5000);     
  } 
  Serial.println("\nConnected.");
}
//read network time
//update internal time to current network time
timeClient.update();
internaltime=timeClient.getEpochTime();
Serial.println("NTP timestamp updated, internal time updated");
}

void loop() {
  // nothing in here
delay(1);
}
