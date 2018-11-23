#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClient.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <MAX6675_Thermocouple.h>

#define VERSION "1.6"
#define ONE_WIRE_BUS D3 //Pin to which is attached a temperature sensor
#define ONE_WIRE_MAX_DEV 3 //The maximum number of devices
#define RELE1 D5
#define LED_GREEN D7
#define RELE2 D6
#define LED_RED D8
#define KAZAN "28ff2dbda416041f" //kazán
#define PUFFER_1_3M "28ff7984011703ca" //puffer 1 (5M)
#define PUFFER_2_5M "28ff43be601703ae" //puffer 2 (3M)

//K-Type definition 
int SCK_PIN = D1;
int CS_PIN = D2;
int SO_PIN = D4;
MAX6675_Thermocouple thermocouple(SCK_PIN, CS_PIN, SO_PIN);

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature DS18B20(&oneWire);
DeviceAddress devAddr[ONE_WIRE_MAX_DEV];  //An array device temperature sensors
float tempDev[ONE_WIRE_MAX_DEV]; //Saving the last measurement of temperature
long lastTemp; //The last measurement
long lastCheck; //The last check kazan and puffer temps
const int durationTemp = 5000; //The frequency of temperature measurement
const int durationCheck = 30000; //The frequency of check kazan and puffer temps
int kazanSzivOn = 0;
int biztonsagiSzivOn = 0;

//WIFI
const char* ssid = "GM_Net";
const char* password = "134679825";
WiFiClient client;

//HTTP
ESP8266WebServer server(80);

//Thingspeak settings
const int channelID = 391628;
String writeAPIKey = "14JG781773HS7E82";
const char* tsServer = "api.thingspeak.com";

//Thinkgspeak update
void UdateThinkSpeakChannel (float kazanTemp, float pufferUpTemp, float pufferDownTemp, float fustGazTemp) {
  if (client.connect(tsServer, 80)) {

    // Construct API request body
    String postStr = writeAPIKey;
    postStr += "&field1=";
    postStr += String(kazanTemp);
    postStr += "&field2=";
    postStr += String(pufferUpTemp);
    postStr += "&field3=";
    postStr += String(pufferDownTemp);
    postStr += "&field4=";
    postStr += String(kazanSzivOn);
    postStr += "&field5=";
    postStr += String(biztonsagiSzivOn);
    postStr += "&field6=";
    postStr += String(fustGazTemp);
    postStr += "\r\n\r\n";

    client.print("POST /update HTTP/1.1\n");
    client.print("Host: api.thingspeak.com\n");
    client.print("Connection: close\n");
    client.print("X-THINGSPEAKAPIKEY: " + writeAPIKey + "\n");
    client.print("Content-Type: application/x-www-form-urlencoded\n");
    client.print("Content-Length: ");
    client.print(postStr.length());
    client.print("\n\n");
    client.print(postStr);
    client.print("\n\n");

  }
  client.stop();
}

//Convert device id to String
String GetAddressToString(DeviceAddress deviceAddress) {
  String str = "";
  for (uint8_t i = 0; i < 8; i++) {
    if ( deviceAddress[i] < 16 ) str += String(0, HEX);
    str += String(deviceAddress[i], HEX);
  }
  return str;
}

//Setting the temperature sensor
void SetupDS18B20() {
  DS18B20.begin();

  Serial.print("Parasite power is: ");
  if ( DS18B20.isParasitePowerMode() ) {
    Serial.println("ON");
  } else {
    Serial.println("OFF");
  }

  Serial.print( "Device count: " );
  Serial.println( DS18B20.getDeviceCount() );

  lastTemp = millis();
  DS18B20.requestTemperatures();

  // Loop through each device, print out address
  for (int i = 0; i < ONE_WIRE_MAX_DEV; i++) {
    // Search the wire for address
    if ( DS18B20.getAddress(devAddr[i], i) ) {
      //devAddr[i] = tempDeviceAddress;
      Serial.print("Found device ");
      Serial.print(i, DEC);
      Serial.print(" with address: " + GetAddressToString(devAddr[i]));
      Serial.println();
    } else {
      Serial.print("Found ghost device at ");
      Serial.print(i, DEC);
      Serial.print(" but could not detect address. Check power and cabling");
    }

    //Get resolution of DS18b20
    Serial.print("Resolution: ");
    Serial.print(DS18B20.getResolution( devAddr[i] ));
    Serial.println();

    //Read temperature from DS18b20
    float tempC = DS18B20.getTempC( devAddr[i] );
    Serial.print("Temp C: ");
    Serial.println(tempC);
  }
}

//Loop measuring the temperature
void TempLoop(long now) {
  if ( now - lastTemp > durationTemp ) { //Take a measurement at a fixed time (durationTemp = 5000ms, 5s)
    float kazanTempC;
    float puffer1TempC;
    float puffer2TempC;
    float fustGazTempC = thermocouple.readCelsius();

    for (int i = 0; i < ONE_WIRE_MAX_DEV; i++) {
      if (GetAddressToString( devAddr[i] ) == KAZAN ) {
        kazanTempC = DS18B20.getTempC( devAddr[i] );
        tempDev[i] = kazanTempC;
      } else if (GetAddressToString( devAddr[i] ) == PUFFER_1_3M) {
        puffer1TempC = DS18B20.getTempC( devAddr[i] );
        tempDev[i] = puffer1TempC;
      } else if (GetAddressToString( devAddr[i] ) == PUFFER_2_5M) {
        puffer2TempC = DS18B20.getTempC( devAddr[i] );
        tempDev[i] = puffer2TempC;
      }
    }

    // Safety check. If kazanTemp too high, then kazanSziv is on immediately.
    if (kazanTempC > 88) {
      digitalWrite(RELE1, LOW);
      digitalWrite(LED_GREEN, HIGH);
      kazanSzivOn = 1;
    }

    if (now - lastCheck > durationCheck) { //Check kazan and puffer temps in fixed time
      //TODO calculate with fustGazTempC
      
      float pufferAvg = (puffer1TempC + puffer2TempC) / 2;

      if (kazanSzivOn == 1) {
        bool turnOff = (kazanTempC <= 88) && ((kazanTempC < pufferAvg - 1) || (kazanTempC < puffer1TempC - 3) || (kazanTempC < puffer2TempC));
        if (turnOff || kazanTempC < 63) {
          digitalWrite(RELE1, HIGH);
          digitalWrite(LED_GREEN, LOW);
          kazanSzivOn = 0;
        }
      } else {
        bool turnOn = (kazanTempC > 64) && (kazanTempC > pufferAvg + 1) && (kazanTempC >= puffer1TempC - 3) && (kazanTempC >= puffer2TempC);
        if (turnOn || (kazanTempC > 88)) {
          digitalWrite(RELE1, LOW);
          digitalWrite(LED_GREEN, HIGH);
          kazanSzivOn = 1;
        }
      }



      if (puffer2TempC > 88) {
        digitalWrite(RELE2, LOW);
        digitalWrite(LED_RED, HIGH);
        biztonsagiSzivOn = 1;
      } else {
        digitalWrite(RELE2, HIGH);
        digitalWrite(LED_RED, LOW);
        biztonsagiSzivOn = 0;
      }

      UdateThinkSpeakChannel(kazanTempC, puffer1TempC, puffer2TempC, fustGazTempC);
      
      lastCheck = millis();
    }

    DS18B20.setWaitForConversion(false); //No waiting for measurement
    DS18B20.requestTemperatures(); //Initiate the temperature measurement
    lastTemp = millis();  //Remember the last time measurement
  }
}

void HandleRoot() {
  String message = "<h1>Kazan vezerlo rendszer</h1>";
  message += "\r\n<br>";
  message += "<h3>version";
   
  message += VERSION; 
  message += "</h3>";
  message += "\r\n<br>";
  char temperatureString[6];

  message += "<table border='1'>\r\n";
  message += "<tr><td>Homero nev</td><td>Device id</td><td>Temperature</td></tr>\r\n";
  for (int i = 0; i < ONE_WIRE_MAX_DEV; i++) {
    dtostrf(tempDev[i], 2, 2, temperatureString);
    Serial.print( "Sending temperature: " );
    Serial.println( temperatureString );


    String devAddress = GetAddressToString( devAddr[i] );
    String nameOfDev = "";
    if (devAddress == KAZAN) {
      nameOfDev = "Kazan";
    } else if (devAddress == PUFFER_1_3M) {
      nameOfDev = "Puffer 1 (3M)";
    } else if (devAddress == PUFFER_2_5M) {
      nameOfDev = "Puffer 2 (5M)";
    }

    message += "<tr><td>";
    message += nameOfDev;
    message += "</td>\r\n";
    message += "<td>";
    message += devAddress;
    message += "</td>\r\n";
    message += "<td>";
    message += temperatureString;
    message += "</td></tr>\r\n";
    message += "\r\n";
  }
  message += "</table>\r\n";

  String kazanSzivOnString;
  if (kazanSzivOn == 1) {
    kazanSzivOnString = "On";
  } else {
    kazanSzivOnString = "Off";
  }

  String biztonsagiSzivOnString;
  if (biztonsagiSzivOn == 1) {
    biztonsagiSzivOnString = "On";
  } else {
    biztonsagiSzivOnString = "Off";
  }

  message += "<br/><br/>";
  message += "<table border='1'>\r\n";
  message += "<tr><td>Szivattyu</td><td>On/Off</td></tr>\r\n";
  message += "<tr><td>Kazan</td><td>" + kazanSzivOnString + "</td></tr>\r\n";
  message += "<tr><td>Biztonsagi (Radiator)</td><td>" + biztonsagiSzivOnString + "</td></tr>\r\n";
  message += "</table>\r\n";

  server.send(200, "text/html", message );
}

void HandleNotFound() {
  String message = "File Not Found\n\n";
  server.send(404, "text/html", message);
}


//------------------------------------------
void setup() {
  //Setup Serial port speed
  Serial.begin(115200);

  pinMode(RELE1, OUTPUT);
  pinMode(RELE2, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_RED, OUTPUT);

  digitalWrite(RELE1, HIGH);
  digitalWrite(RELE2, HIGH);
  digitalWrite(LED_GREEN, LOW);
  digitalWrite(LED_RED, LOW);

  //Setup WIFI
  WiFi.begin(ssid, password);
  Serial.println("");

  //Wait for WIFI connection
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.print("Connected to ");
  Serial.println(ssid);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  server.on("/", HandleRoot);
  server.onNotFound( HandleNotFound );
  server.begin();
  Serial.println("HTTP server started at ip " + WiFi.localIP().toString() );

  //Setup DS18b20 temperature sensor
  SetupDS18B20();
}

void loop() {
  long t = millis();

  server.handleClient();
  TempLoop( t );
}
