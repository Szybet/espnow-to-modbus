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

uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
                                             0xFF, 0xFF, 0xFF};

uint8_t data_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00};
xQueueHandle s_espnow_queue;

// static void espnow_deinit(espnow_send *send_param);

void init_gpio() {
  // zero-initialize the config structure.
  gpio_config_t io_conf = {};
  // disable interrupt
  io_conf.intr_type = GPIO_INTR_DISABLE;
  // set as output mode
  io_conf.mode = GPIO_MODE_OUTPUT;
  // bit mask of the pins that you want to set,e.g.GPIO18/19
  io_conf.pin_bit_mask = 1ULL << GPIO_NUM_2;
  // disable pull-down mode
  io_conf.pull_down_en = 0;
  // disable pull-up mode
  io_conf.pull_up_en = 0;
  // configure GPIO with the given settings

  gpio_reset_pin(GPIO_NUM_2);
  gpio_config(&io_conf);
  gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);
  ESP_LOGI(TAG, "Started GPIO");
}

void init_uart() {
  uart_config_t uart_config = {
      .baud_rate = 1200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_EVEN,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
      //.rx_flow_ctrl_thresh = 122,
      .source_clk = UART_SCLK_APB,
  };
  int intr_alloc_flags = 0;

#if CONFIG_UART_ISR_IN_IRAM
  intr_alloc_flags = ESP_INTR_FLAG_IRAM;
#endif
  ESP_LOGI(TAG, "intr_alloc_flags %d", intr_alloc_flags);
  ESP_ERROR_CHECK(
      uart_driver_install(UART_NUM_2, 256, 0, 0, NULL, intr_alloc_flags));

  ESP_ERROR_CHECK(uart_param_config(UART_NUM_2, &uart_config));

  ESP_ERROR_CHECK(uart_set_pin(UART_NUM_2, 17, 16, -1, -1));

  // ESP_ERROR_CHECK(uart_set_mode(UART_NUM_2, UART_MODE_RS485_HALF_DUPLEX));

  // ESP_ERROR_CHECK(uart_set_rx_timeout(UART_NUM_2, 3));
  ESP_LOGI(TAG, "Started uart");
}

// Manage Wifi
static void wifi_init(void) {
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_mode(ESPNOW_WIFI_MODE));
  ESP_ERROR_CHECK(esp_wifi_set_protocol(ESPNOW_WIFI_IF, WIFI_PROTOCOL_LR));
  ESP_ERROR_CHECK(esp_wifi_start());
  ESP_LOGI(TAG, "Started Wifi");
}

void espnow_deinit_func(espnow_send *send_param) {
  free(send_param->buffer);
  free(send_param);
  vSemaphoreDelete(s_espnow_queue);
  esp_now_deinit();
}

// Manage wifi end

// Use espnow

static void espnow_task(void *pvParameter) {
  espnow_send *send_param_broadcast =
      (espnow_send *)pvParameter; // this is broadcast always

  espnow_event_t evt;

  vTaskDelay(5000 / portTICK_RATE_MS);

  uint8_t request[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x06, 0xC5, 0xC8};

  while (true) {

    uart_send_data(request, 8);

    uint8_t received_data[250];

    int received_data_len = uart_receive_data(received_data, 250);

    if (received_data_len > 0) {

      for (int i = 0; i < received_data_len; i++) {
        ESP_LOGI(TAG, "Byte %d: %d", i, (int)received_data[i]);
      }

      espnow_send *send_param_uart_data =
          espnow_data_create(data_mac, received_data, received_data_len);

      esp_now_send(send_param_uart_data->dest_mac, send_param_uart_data->buffer,
                   send_param_uart_data->len);
    }
    // free(*received_data);

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

        char *message = (char *)recv_cb->data;
        ESP_LOGI(TAG, "Received message: %s", message);

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

static esp_err_t espnow_init_minimal(void) {
  s_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
  if (s_espnow_queue == NULL) {
    ESP_LOGE(TAG, "Create mutex fail");
    return ESP_FAIL;
  }

  /* Initialize ESPNOW and register sending and receiving callback function. */
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

  /* Set primary master key. */
  ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)ESPNOW_PMK));

  /* Add broadcast peer information to peer list. */
  espnow_addpeer(s_broadcast_mac);

  // xTaskCreate(espnow_task, "espnow_task", 2048, send_param, 4, NULL);

  ESP_LOGI(TAG, "Exiting espnow minimal init");
  return ESP_OK;
}

// Use espnow end

void app_main(void) {
  // Initialize NVS
  esp_err_t ret = nvs_flash_init();
  if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
      ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    ret = nvs_flash_init();
  }
  ESP_ERROR_CHECK(ret);

  wifi_init();

  unsigned char mac_base[6] = {0};
  esp_efuse_mac_get_default(mac_base);
  esp_read_mac(mac_base, ESP_MAC_WIFI_STA);
  unsigned char mac_local_base[6] = {0};
  unsigned char mac_uni_base[6] = {0};
  esp_derive_local_mac(mac_local_base, mac_uni_base);
  printf("Local Address: ");
  print_mac(mac_local_base);
  printf("\nUni Address: ");
  print_mac(mac_uni_base);
  printf("\nMAC Address: ");
  print_mac(mac_base);
  printf("\n");

  espnow_init_minimal();
  vTaskDelay(1500 / portTICK_RATE_MS);
  init_uart();
  vTaskDelay(1500 / portTICK_RATE_MS);
  init_gpio();

  char message[] = "broadcast";
  int array_size_chars = sizeof(message) / sizeof(message[0]);
  uint8_t *array_bytes = (uint8_t *)message;

  espnow_send *send_param =
      espnow_data_create(s_broadcast_mac, array_bytes, array_size_chars);

  xTaskCreate(espnow_task, "espnow_task", 2048, send_param, 4, NULL);
  ESP_LOGI(TAG, "created task");
}
