#include <Arduino.h>

// https://www.arduino.cc/reference/en/libraries/wifi/server.available/
#include <ESP8266WiFi.h>
#include <SoftwareSerial.h>

const char *ssid = "dragonn_EXT";
const char *password = "ca9hi6HX";

// rx = GPIO8, tx = GPIO2
SoftwareSerial logSerial(13, 2);

uint8_t ExampleRequest[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0xC5, 0xC8};

WiFiServer server(5000);
WiFiClient client;

void setup() {
  Serial.begin(9600);
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
  server.begin();
}

size_t read_serial(uint8_t *buffer, size_t buffer_size);

void loop() {
  if (!client) {
    client = server.available();
  }

  if (client) {
    if (client.available() > 0) {
      int readClient = client.available();
      logSerial.print("Available are ");
      logSerial.print(readClient);
      logSerial.println(" Bytes to read from client");

      if (readClient >= 8) {
        uint8_t tcpBytes[255];
        // reading those bytes
        bool reading = true;
        int count = 0;
        while (reading == true) {
          int32_t readedByte = client.read();
          if (readedByte != -1) {
            tcpBytes[count] = readedByte;
          } else {
            reading = false;
          }
          count = count + 1;
        }
        logSerial.print("Received: ");
        for (int i = 0; i < readClient; i++) {
          logSerial.print(tcpBytes[i], HEX);
          logSerial.print(" ");
        }
        logSerial.println("");

        logSerial.println("Sending to uart");
        Serial.write(tcpBytes, readClient);
        Serial.flush();

        uint8_t received_data[255];
        size_t readed = read_serial(received_data, sizeof(received_data));

        if (readed > 0) {
          logSerial.print("Readed ");
          logSerial.print(readed);
          logSerial.println(" bytes from uart ");
          for(uint8_t i = 0; i < readed; i++) {
            client.write(received_data[i]);
          }
          logSerial.println("sended to tcp");
        } else {
          logSerial.println("No data from uart");
        }
      }
    }
  }
}

size_t read_serial(uint8_t *buffer, size_t buffer_size) {
  size_t readed = 0;
  
  Serial.setTimeout(4000);
  while(readed < buffer_size) {
    int current_read = Serial.readBytes(&buffer[readed], 1);
    if (current_read > 0) {
      Serial.setTimeout(100);
      readed++;
    } else {
      break;
    }
  }
  return readed;
}