#include <Arduino.h>

// https://www.arduino.cc/reference/en/libraries/wifi/server.available/
#include <ESP8266WiFiMulti.h>
#include <SoftwareSerial.h>
extern "C" {
#include <user_interface.h>
}

ESP8266WiFiMulti wifiMulti;

// rx = GPIO8, tx = GPIO2
SoftwareSerial logSerial(13, 2);

WiFiServer server(5000);
WiFiClient tcp_client;

#define TCP_TIMEOUT 100
#define SERIAL_TIMEOUT 200

unsigned long serial_last_read = 0;
uint8_t serial_count = 0;
uint8_t serial_buffer[255];

unsigned long tcp_last_read = 0;
uint8_t tcp_count = 0;
uint8_t tcp_buffer[255];

uint8_t newMACAddress[] = {0x9A, 0xDA, 0xC4, 0x9D, 0x4B, 0x00};



void (*resetFunc)(void) = 0;

void setup() {
  Serial.begin(9600);
  logSerial.begin(115200);

  logSerial.println("Starting");


  WiFi.mode(WIFI_STA);

  wifi_set_macaddr(STATION_IF, newMACAddress);

  

  // WiFi connect timeout per AP. Increase when connecting takes longer.
  wifiMulti.addAP("dragonn_EXT", "ca9hi6HX");
  wifiMulti.addAP("dragonn", "ca9hi6HX");
  wifiMulti.addAP("dragonn2", "Twb3MRYd");

  server.begin();
}

size_t read_serial(uint8_t *buffer, size_t buffer_size);

bool client_connected_status = true;

bool connected_wifi_log = false;

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (connected_wifi_log == false) {
      connected_wifi_log = true;
      logSerial.print("Connected to: ");
      logSerial.println(WiFi.SSID());
      logSerial.print("IP Address is: ");
      logSerial.println(WiFi.localIP());

      logSerial.print("Wifi strength is: ");
      long rssi = WiFi.RSSI();
      logSerial.println(rssi);

      byte mac[6];
      WiFi.macAddress(mac);
      logSerial.print("MAC is: ");
      logSerial.print(mac[5], HEX);
      logSerial.print(":");
      logSerial.print(mac[4], HEX);
      logSerial.print(":");
      logSerial.print(mac[3], HEX);
      logSerial.print(":");
      logSerial.print(mac[2], HEX);
      logSerial.print(":");
      logSerial.print(mac[1], HEX);
      logSerial.print(":");
      logSerial.println(mac[0], HEX);
    }

    if (!tcp_client) {
      tcp_client = server.available();
      if (client_connected_status == true) {
        client_connected_status = false;
        logSerial.println("No client connected");
      }
    }

    if (tcp_client) {
      if (client_connected_status == false) {
        client_connected_status = true;
        logSerial.println("client connected");
      }
      size_t tcp_available = tcp_client.available();
      if (tcp_available + tcp_count > sizeof(tcp_buffer)) {
        tcp_available = tcp_available - (sizeof(tcp_buffer) - tcp_count);
      }
      if (tcp_available > 0) {
        tcp_client.readBytes(&tcp_buffer[tcp_count], tcp_available);
        tcp_count += tcp_available;
        tcp_last_read = millis();
      }
    }

    if (tcp_count > 0 && ((millis() - tcp_last_read) > TCP_TIMEOUT ||
                          tcp_count == sizeof(tcp_buffer))) {
      Serial.write(tcp_buffer, tcp_count);
      logSerial.println("Sended bytes to serial from tcp");
      tcp_count = 0;
    }

    size_t serial_available = Serial.available();
    if (serial_available + serial_count > sizeof(serial_buffer)) {
      serial_available =
          serial_available - (sizeof(serial_buffer) - serial_count);
    }

    if (serial_available > 0) {
      Serial.readBytes(&serial_buffer[serial_count], serial_available);
      serial_count += serial_available;
      serial_last_read = millis();
    }

    if (serial_count > 0 && ((millis() - serial_last_read) > SERIAL_TIMEOUT ||
                             serial_count == sizeof(serial_buffer))) {
      tcp_client.write(serial_buffer, serial_count);
      logSerial.println("Sended bytes to tcp from serial");
      serial_count = 0;
    }
  } else {
    connected_wifi_log = false;
    logSerial.println("No wifi connection");
    delay(500);
    wifiMulti.run(5000);
  }
}