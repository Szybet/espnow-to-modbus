#include "esp_crc.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "nvs_flash.h"
#include <assert.h>
#include <iso646.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "sdkconfig.h"

#include "driver/gpio.h"

#include "debug.h"
#include "espnow_esp.h"
#include "espnow_manage_data.h"
#include "main_settings.h"
#include "uart_data.h"

// uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0xC5, 0xC8};

void espnow_communication(void *pvParameter) {
  espnow_send *send_param_broadcast =
      (espnow_send *)pvParameter; // this is broadcast always

  espnow_event_t evt;

  vTaskDelay(5000 / portTICK_RATE_MS);

  bool sended = false;

  while (true) {
    uint8_t received_data[250];
    int received_data_len = 0;
    if (sended) {
      received_data_len = uart_receive_data(received_data, 250, 10, 10);
      if (received_data_len > 0) {
        ESP_LOGI(TAG, "Got uart data while waiting for espnow response: %d", received_data_len);
        received_data_len += uart_receive_data(&received_data[received_data_len], 250 - received_data_len, 2000, 100);
        sended = false;
      }
    } else {
      received_data_len = uart_receive_data(received_data, 250, 2000, 100);
    }

    if (received_data_len > 0) {
      ESP_LOGI(TAG, "Received data from uart: %d", received_data_len);

      for (int i = 0; i < received_data_len; i++) {
        ESP_LOGI(TAG, "Received %d byte: %02X", i, received_data[i]);
      }

      espnow_send *send_param_uart_data = espnow_data_create(
          send_param_broadcast->dest_mac, received_data, received_data_len);

      ESP_LOGI(TAG, "Sending Data to espnow");
      espnow_send_smarter(send_param_uart_data);

      sended = true;
    }

    while (xQueueReceive(s_espnow_queue, &evt, 100) == pdTRUE) {
      switch (evt.id) {
      case ESPNOW_SEND_CB: {
        espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
        ESP_LOGI(TAG, "Send data to " MACSTR "", MAC2STR(send_cb->mac_addr));

        break;
      }
      case ESPNOW_RECV_CB: {
        espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

        ESP_LOGI(TAG, "Received message from: " MACSTR " with lenght of: %d",
                 MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

        espnow_addpeer(recv_cb->mac_addr);

        for (int i = 0; i < recv_cb->data_len; i++) {
          ESP_LOGI(TAG, "Received %d byte: %02X", i, recv_cb->data[i]);
        }

        uart_send_data(recv_cb->data, recv_cb->data_len);

        free(recv_cb->data);
        sended = false;
        break;
      }
      default:
        ESP_LOGI(TAG, "Callback type error: %d", evt.id);
        break;
      }
    }
  }
}