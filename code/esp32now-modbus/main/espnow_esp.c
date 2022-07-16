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

#include "esp_task_wdt.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

void espnow_communication(void *arg) {
  // Subscribe this task to TWDT, then check if it is subscribed
  ESP_ERROR_CHECK(esp_task_wdt_add(NULL));
  ESP_ERROR_CHECK(esp_task_wdt_status(NULL));

  // Reset it after entering task
  //esp_task_wdt_reset();
  ESP_LOGI(TAG, "Started espnow_communication task");

  // Create broadcast
  char message[] = "111 broadcast 111";
  int array_size_chars = sizeof(message) / sizeof(message[0]);
  uint8_t *array_bytes = (uint8_t *)message;

  espnow_send *send_param_broadcast =
      espnow_data_create(s_broadcast_mac, array_bytes, array_size_chars);

  espnow_event_t evt;
  vTaskDelay(5000 / portTICK_RATE_MS);
  // Reset it after needed espnow pause
  esp_task_wdt_reset();

  int64_t uart_last_read = esp_timer_get_time();
  uint8_t uart_count = 0;
  uint8_t uart_buffer[255];

  ESP_LOGI(TAG, "Started espnow_communication loop");
  while (true) {
    size_t uart_available = 0;
    ESP_ERROR_CHECK(uart_get_buffered_data_len(UART_NUM_2, &uart_available));
    if (uart_available + uart_count > sizeof(uart_buffer)) {
      uart_available = uart_available - (sizeof(uart_buffer) - uart_count);
    }

    if (uart_available > 0) {
      // Reset watchdog before reading from uart
      esp_task_wdt_reset();
      uart_read_bytes(UART_NUM_2, &uart_buffer[uart_count], uart_available,
                      100);
      uart_count += uart_available;
      uart_last_read = esp_timer_get_time();
      ESP_LOGI(TAG, "Received uart data");
    }

    if (uart_count > 0 &&
        ((esp_timer_get_time() - uart_last_read) > UART_TIMEOUT ||
         uart_count == sizeof(uart_buffer))) {
      espnow_send *send_param_uart_data = espnow_data_create(
          send_param_broadcast->dest_mac, uart_buffer, uart_count);

      ESP_LOGI(TAG, "Sending Data to espnow");
      espnow_send_smarter(send_param_uart_data);
      // Reset watchdog after sending
      esp_task_wdt_reset();
      uart_count = 0;
    }

    while (xQueueReceive(s_espnow_queue, &evt, 5) == pdTRUE) {
      switch (evt.id) {
      case ESPNOW_SEND_CB: {
        espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
        ESP_LOGI(TAG, "Send data to " MACSTR "", MAC2STR(send_cb->mac_addr));

        break;
      }
      case ESPNOW_RECV_CB: {
        // Reset watchdog after receiving espnow message
        esp_task_wdt_reset();

        espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

        ESP_LOGI(TAG, "Received message from: " MACSTR " with lenght of: %d",
                 MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

        espnow_addpeer(recv_cb->mac_addr);

        if (BYTE_LOGS == true) {
          for (int i = 0; i < recv_cb->data_len; i++) {
            ESP_LOGI(TAG, "Received %d byte: %02X", i, recv_cb->data[i]);
          }
        }

        uart_send_data(recv_cb->data, recv_cb->data_len);

        free(recv_cb->data);
        break;
      }
      default:
        ESP_LOGI(TAG, "Callback type error: %d", evt.id);
        break;
      }
    }
  }
}