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

#include "debug.h"
#include "espnow_manage_data.h"
#include "main_settings.h"

uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN] = {0xFF, 0xFF, 0xFF,
                                             0xFF, 0xFF, 0xFF};
// uint16_t s_espnow_seq[ESPNOW_DATA_MAX] = {0, 0};
uint16_t s_espnow_seq[2] = {0, 0};

xQueueHandle s_espnow_queue;

// static void espnow_deinit(espnow_send_param_t *send_param);

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

void espnow_deinit_func(espnow_send_param_t *send_param) {
  free(send_param->buffer);
  free(send_param);
  vSemaphoreDelete(s_espnow_queue);
  esp_now_deinit();
}

// Manage wifi end

// Use espnow

static void espnow_task(void *pvParameter) {
  espnow_send_param_t *send_param_broadcast =
      (espnow_send_param_t *)pvParameter; // this is broadcast always

  espnow_event_t evt;
  uint8_t recv_state = 0;
  uint16_t recv_seq = 0;
  int recv_magic = 0;
  bool is_broadcast = false;
  int ret;

  vTaskDelay(5000 / portTICK_RATE_MS);

  while (true) {
    esp_now_send(send_param_broadcast->dest_mac, send_param_broadcast->buffer,
                 send_param_broadcast->len);
    while (xQueueReceive(s_espnow_queue, &evt, 1000) == pdTRUE) {
      switch (evt.id) {
      case ESPNOW_SEND_CB: {
        espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
        is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

        ESP_LOGI(TAG, "Send data to " MACSTR ", status: %d",
                 MAC2STR(send_cb->mac_addr), send_cb->status);

        break;
      }
      case ESPNOW_RECV_CB: {
        espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
        ret = espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state,
                                &recv_seq, &recv_magic);
        free(recv_cb->data);

        ESP_LOGI(TAG, "Add peer: " MACSTR "", MAC2STR(recv_cb->mac_addr));

        if (esp_now_is_peer_exist(recv_cb->mac_addr) == false) {
          espnow_addpeer(recv_cb->mac_addr);
        } else {
          ESP_LOGI(TAG, "Peer is already added");
        }

        char message[] = "esp message 123 aaa";
        int array_size_chars = sizeof(message) / sizeof(message[0]);
        uint8_t array_bytes[array_size_chars];

        for (int i = 0; i < array_size_chars; i++) {
          array_bytes[i] = (uint8_t)message[i];
        }
        int array_size_bytes = sizeof(array_bytes);

        espnow_send_param_t *send_param =
            espnow_data_create(recv_cb->mac_addr, array_bytes, array_size_bytes);

        esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len);

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
  // malloc - Allocates size bytes of uninitialized storage.
  esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
  if (peer == NULL) {
    ESP_LOGE(TAG, "Malloc peer information fail");
    vSemaphoreDelete(s_espnow_queue);
    esp_now_deinit();
    return ESP_FAIL;
  }

  // https://en.cppreference.com/w/cpp/string/byte/memset
  memset(peer, 0, sizeof(esp_now_peer_info_t));
  peer->channel = ESPNOW_CHANNEL;
  peer->ifidx = ESPNOW_WIFI_IF;
  peer->encrypt = false;
  memcpy(peer->peer_addr, s_broadcast_mac, ESP_NOW_ETH_ALEN);
  ESP_ERROR_CHECK(esp_now_add_peer(peer));
  free(peer);

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

  char message[] = "esp";
  int array_size_chars = sizeof(message) / sizeof(message[0]);
  uint8_t array_bytes[array_size_chars];

  for (int i = 0; i < array_size_chars; i++) {
    array_bytes[i] = (uint8_t)message[i];
  }
  int array_size_bytes = sizeof(array_bytes); // / sizeof(array_bytes[0]);

  ESP_LOGI(TAG, "array_size_bytes: %d", array_size_bytes);

  espnow_send_param_t *send_param =
      espnow_data_create(s_broadcast_mac, array_bytes, array_size_bytes);

  xTaskCreate(espnow_task, "espnow_task", 2048, send_param, 4, NULL);
  ESP_LOGI(TAG, "created task");

  // espnow_init_debug();
}
