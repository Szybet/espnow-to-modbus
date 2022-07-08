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
WiFiClient tcp_client;

#define TCP_TIMEOUT 100
#define SERIAL_TIMEOUT 200

unsigned long serial_last_read = 0;
uint8_t serial_count = 0;
uint8_t serial_buffer[255];

unsigned long tcp_last_read = 0;
uint8_t tcp_count = 0;
uint8_t tcp_buffer[255];

void (*resetFunc)(void) = 0;

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

bool client_connected_status = true;

void loop() {
  if (!tcp_client) {
    tcp_client = server.available();
    if(client_connected_status == true)
    {
      client_connected_status = false;
      logSerial.println("No client connected");
    }
  }

  if (tcp_client) {
    if(client_connected_status == false)
    {
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
      logSerial.println("Readed bytes from tcp");
    }
  }

  if (tcp_count > 0 && ((millis() - tcp_last_read) > TCP_TIMEOUT ||
                        tcp_count == sizeof(tcp_buffer))) {
    Serial.write(tcp_buffer, tcp_count);
    logSerial.println("Sended bytes to serial");
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
    logSerial.println("Readed bytes from serial");
  }

  if (serial_count > 0 && ((millis() - serial_last_read) > SERIAL_TIMEOUT ||
                           serial_count == sizeof(serial_buffer))) {
    tcp_client.write(serial_buffer, serial_count);
    logSerial.println("Sended bytes to tcp");
    serial_count = 0;
  }

  // Check if wifi is connected
  if (WiFi.status() != WL_CONNECTED) {
    logSerial.print("Wifi dissconected?");
    delay(500);
    if (WiFi.status() != WL_CONNECTED) {
      logSerial.print("Wifi dissconected, resseting");
      resetFunc();
    }
  }
}