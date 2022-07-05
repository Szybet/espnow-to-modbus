#include <Arduino.h>

#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>
// Replace these with your WiFi network settings
const char *ssid = "hotspot"; // replace this with your WiFi network name
const char *password =
    "12345678"; // replace this with your WiFi network password

// rx = GPIO8, tx = GPIO2
SoftwareSerial logSerial(13, 2);

uint8_t ExampleRequest[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0xC5, 0xC8};

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(500);

  logSerial.begin(115200);

  logSerial.println("Starting");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    logSerial.print("Connecting to ");
    logSerial.print(ssid);
    logSerial.print(" With password: ");
    logSerial.println(password);
  }
  logSerial.println("Connected to wifi");
  logSerial.print("IP Address is: ");
  logSerial.println(WiFi.localIP());
}

void loop() {

  logSerial.println("Looping");
  delay(4000);

  logSerial.println("Sending to uart");
  Serial.write(ExampleRequest, 8);
  Serial.flush();

  uint8_t received_data[250];
  int readed = Serial.readBytes(received_data, 250);

  if (readed > 0) {
    logSerial.print("Readed ");
    logSerial.print(readed);
    logSerial.println("bytes from uart ");
  } else {
    logSerial.println("No data from uart");
  }
}