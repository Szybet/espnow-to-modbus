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
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "driver/gpio.h"
#include "driver/uart.h"
#include "sdkconfig.h"

#include "driver/gpio.h"

#include "debug.h"
#include "espnow_manage_data.h"
#include "main_settings.h"
#include "uart_data.h"

void uart_send_data(uint8_t *data_to_send, int data_size) {
  gpio_set_level(GPIO_NUM_2, 1);

  vTaskDelay(100 / portTICK_RATE_MS);

  uart_write_bytes(UART_NUM_2, data_to_send, data_size);
  uart_wait_tx_done(UART_NUM_2, 20 / portTICK_RATE_MS);

  gpio_set_level(GPIO_NUM_2, 0);
}

size_t uart_receive_data(uint8_t *buffer, size_t buffer_size) {

  //uint8_t *response = (uint8_t *)malloc(256);

  int readed =
      uart_read_bytes(UART_NUM_2, buffer, buffer_size, 500 / portTICK_RATE_MS);
  if (readed > 0) {
    ESP_LOGI(TAG, "Received %d bytes of data from uart", readed);
  } else {
    ESP_LOGI(TAG, "No response from uart");
    return 0;
  }
  return readed;
}