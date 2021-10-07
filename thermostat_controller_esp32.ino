#include "secrets.h"
#include <WiFiClientSecure.h>
#include <MQTTClient.h>
#include <ArduinoJson.h>
#include "WiFi.h"
#include "DHT.h"

// pins
#define DHTTYPE DHT22
#define DHT_PIN 32
#define S1 15
#define S2 17
#define LED 21

// Topics to publish to
#define GET_TOPIC "$aws/things/thermostat_controller_esp32/shadow/get"
#define UPDATE_TOPIC "$aws/things/thermostat_controller_esp32/shadow/update"

// Topics to subscribe to
#define GET_ACCEPTED_TOPIC    "$aws/things/thermostat_controller_esp32/shadow/get/accepted"
#define GET_REJECTED_TOPIC    "$aws/things/thermostat_controller_esp32/shadow/get/rejected"
#define UPDATE_DELTA_TOPIC    "$aws/things/thermostat_controller_esp32/shadow/update/delta"
#define UPDATE_ACCEPTED_TOPIC "$aws/things/thermostat_controller_esp32/shadow/update/accepted"
#define UPDATE_REJECTED_TOPIC "$aws/things/thermostat_controller_esp32/shadow/update/rejected"
#define UPDATE_DOCUMENTS      "$aws/things/thermostat_controller_esp32/shadow/update/documents"

unsigned long currentMillis;
unsigned long logStartMillis;
unsigned long publishStartMillis;
const unsigned long logPeriod = 5000;
const unsigned long publishPeriod = 60000;

long temp = 72;
float measured_temp;

DHT dht(DHT_PIN, DHTTYPE);

WiFiClientSecure net = WiFiClientSecure();
MQTTClient client = MQTTClient(1024);

void setup() {
  // set pin modes
  pinMode (S1, OUTPUT);
  pinMode (S2, OUTPUT);
  pinMode (LED, OUTPUT);
  // start DHT sensor
  dht.begin();
  Serial.begin(9600);
  // connect to Wifi and AWS
  digitalWrite(LED, HIGH);
  connectAWS();
  digitalWrite(LED, LOW);
  // record current time
  logStartMillis = millis();
  publishStartMillis = millis();
}

void loop() {
  currentMillis = millis();
  if(currentMillis - logStartMillis > logPeriod) {
    measured_temp = dht.readTemperature(true);
    Serial.print("\nCurrent temperature: ");
    Serial.println(temp);
    Serial.print("\nMeasured temperature: ");
    Serial.println(measured_temp);
    logStartMillis = currentMillis;
  }
  if(currentMillis - publishStartMillis > publishPeriod) {
    publishUpdateState();
    publishStartMillis = currentMillis;
  }
  // listen for MQTT messages
  client.loop();
  Serial.print(".");
  delay(500);
}

void connectAWS() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Connecting to Wi-Fi");

  while (WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }

  // Configure WiFiClientSecure to use the AWS IoT device credentials
  net.setCACert(AWS_CERT_CA);
  net.setCertificate(AWS_CERT_CRT);
  net.setPrivateKey(AWS_CERT_PRIVATE);

  // Connect to the MQTT broker on the AWS endpoint we defined earlier
  client.begin(AWS_IOT_ENDPOINT, 8883, net);

  // Create a message handler
  client.onMessage(messageHandler);

  Serial.print("Connecting to AWS IOT");

  while (!client.connect(THINGNAME)) {
    Serial.print(".");
    delay(100);
  }

  if(!client.connected()){
    Serial.println("AWS IoT Timeout!");
    return;
  }

  // Subscribe to a topic
  client.subscribe(UPDATE_ACCEPTED_TOPIC);
  client.subscribe(UPDATE_REJECTED_TOPIC);
  client.subscribe(UPDATE_DELTA_TOPIC);
  client.subscribe(GET_ACCEPTED_TOPIC);
  client.subscribe(GET_REJECTED_TOPIC);
  
  Serial.println("AWS IoT Connected!");

  measured_temp = dht.readTemperature(true);
  publishGetState();
}

void publishUpdateState() {
  StaticJsonDocument<200> doc;
  doc["state"]["reported"]["thermostat_temperature"] = temp;
  Serial.println(measured_temp);
  // if measured_temp is NaN due to sensor issue, ESP32 will set this value to null 
  // and measured_temp will be removed from the shadow document  
  doc["state"]["reported"]["measured_temperature"] = measured_temp;
  char jsonBuffer[512];
  serializeJson(doc, jsonBuffer);
  Serial.print("publishing to: ");
  Serial.println(UPDATE_TOPIC);
  client.publish(UPDATE_TOPIC, jsonBuffer);
}

void publishGetState() {
  Serial.print("publishing to: ");
  Serial.println(GET_TOPIC);
  client.publish(GET_TOPIC);
}

void messageHandler(String &topic, String &payload) {
  StaticJsonDocument<1024> doc;
  DeserializationError error = deserializeJson(doc, payload);
  if (error) {
    Serial.print(F("deserializeJson() failed: "));
    Serial.println(error.f_str());
    return;
  }

  Serial.println("incoming: " + topic + " - " + payload);

  if(strcmp (GET_ACCEPTED_TOPIC, topic.c_str()) == 0) {
    temp = doc["state"]["desired"]["thermostat_temperature"];
    Serial.print("Updating temperature to: "); 
    Serial.println(temp);
    publishUpdateState();
  }

  if(strcmp (UPDATE_DELTA_TOPIC, topic.c_str()) == 0) {
    changeTemperature(doc["state"]["thermostat_temperature"]);
  }

}

void changeTemperature(int desired_temp) {
  if (desired_temp < temp) {
      Serial.println("actuating S1");
      digitalWrite(S1, HIGH);
      delay(1500);
      digitalWrite(S1, LOW);
      delay(500);
    while (desired_temp < temp) {
        actuateSolenoid(S1);
        temp -= 1;
    }
  } else {
      Serial.println("actuating S2");
      digitalWrite(S2, HIGH);
      delay(2000);
      digitalWrite(S2, LOW);
      delay(500);
    while (desired_temp > temp) {
        actuateSolenoid(S2);
        temp += 1;
    }
  }
  publishUpdateState();
}

void actuateSolenoid(int id) {
  Serial.println("ACTUATE");
  digitalWrite(id, HIGH);
  delay(250);
  digitalWrite(id, LOW);
  delay(250);
}
