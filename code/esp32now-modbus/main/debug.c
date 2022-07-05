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

#include "espnow_manage_data.h"
#include "main_settings.h"

// var begin

#define ESPNOW_PMK "pmk1234567890123"
// ESPNOW primary master to use. The length of ESPNOW primary master must be 16
// bytes.
#define ESPNOW_LMK "lmk1234567890123"
// ESPNOW local master to use. The length of ESPNOW local master must be 16
// bytes.
#define ESPNOW_CHANNEL 1
// range 0 14
#define ESPNOW_SEND_DELAY 1000
// range 0 65535
// Delay between sending two ESPNOW data, unit: ms.
#define ESPNOW_SEND_LEN 10
// "Send len"


/*
// PvParameter is the argument provided with QTaskCreate
void espnow_task_send(void *pvParameter) {
  espnow_event_t evt;
  uint8_t recv_state = 0;
  uint16_t recv_seq = 0;
  int recv_magic = 0;
  bool is_broadcast = false;
  int ret;

  vTaskDelay(5000 / portTICK_RATE_MS);
  ESP_LOGI(TAG, "Start sending broadcast data");

  while (true) {
    // create new sendparam
    espnow_send_param_t *send_param;
    send_param = malloc(sizeof(espnow_send_param_t));
    memset(send_param, 0, sizeof(espnow_send_param_t));
    if (send_param == NULL) {
      ESP_LOGE(TAG, "Malloc send parameter fail");
      vSemaphoreDelete(s_espnow_queue);
      esp_now_deinit();
      return ESP_FAIL;
    }

    send_param->state = 0;
    send_param->magic = esp_random();
    send_param->len = ESPNOW_SEND_LEN;
    send_param->buffer = malloc(ESPNOW_SEND_LEN);
    if (send_param->buffer == NULL) {
      ESP_LOGE(TAG, "Malloc send buffer fail");
      free(send_param);
      vSemaphoreDelete(s_espnow_queue);
      esp_now_deinit();
      return ESP_FAIL;
    }
    memcpy(send_param->dest_mac, s_broadcast_mac, ESP_NOW_ETH_ALEN);
    espnow_data_prepare(send_param);
    //

    if (esp_now_send(send_param->dest_mac, send_param->buffer,
                     send_param->len) != ESP_OK) {
      ESP_LOGE(TAG, "Send error, Exiting");
      espnow_deinit_func(send_param);
      vTaskDelete(NULL);
    }
    // portMAX_DELAY doesn't worked
    while (xQueueReceive(s_espnow_queue, &evt, 100) == pdTRUE) {
      switch (evt.id) {
      case ESPNOW_SEND_CB: {
        espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
        is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

        ESP_LOGI(TAG, "Send data to " MACSTR ", status: %d",
                 MAC2STR(send_cb->mac_addr), send_cb->status);

        //if (send_param->delay > 0)
        {
            vTaskDelay(send_param->delay / portTICK_RATE_MS);
        //}
        break;
      }
      case ESPNOW_RECV_CB: {
        espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;
        ret = espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state,
                                &recv_seq, &recv_magic);
        espnow_data_t *buf = (espnow_data_t *)recv_cb->data;

        uint16_t crc_cal = 0;
        crc_cal =
            esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, recv_cb->data_len);

        if (ret == ESPNOW_DATA_BROADCAST) {
          ESP_LOGI(
              TAG,
              "Receive %dth broadcast data from: " MACSTR ", len: %d, crc: %d",
              recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len, crc_cal);
        } else if (ret == ESPNOW_DATA_UNICAST) {
          ESP_LOGI(
              TAG,
              "Receive %dth unicast data from: " MACSTR ", len: %d, crc: %d",
              recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len, crc_cal);
        } else {
          ESP_LOGI(TAG, "Receive error data from: " MACSTR "",
                   MAC2STR(recv_cb->mac_addr));
        }
        char message[recv_cb->data_len];

        for (int i = 0; i < recv_cb->data_len; i++) {
          message[i] = buf->payload[i];
        }

        ESP_LOGI(TAG, "Message is: %s", message);

        //
                char message[recv_cb->data_len];

        for (int i = 0; i < recv_cb->data_len; i++) {
          message[i] = recv_cb->data[i] + '0';
          ESP_LOGI(TAG, "for: %u", recv_cb->data[i]);
        }
        ESP_LOGI(TAG, "With message: %s", message);
        /(recv_cb->data);
        //
        break;
      }
      default:
        ESP_LOGI(TAG, "Callback type error: %d", evt.id);
        break;
      }
    }
  }
}

esp_err_t espnow_init_debug() {
  espnow_send_param_t *send_param;

  s_espnow_queue = xQueueCreate(ESPNOW_QUEUE_SIZE, sizeof(espnow_event_t));
  if (s_espnow_queue == NULL) {
    ESP_LOGE(TAG, "Create mutex fail");
    return ESP_FAIL;
  }

  // Initialize ESPNOW and register sending and receiving callback function. 
  ESP_ERROR_CHECK(esp_now_init());
  ESP_ERROR_CHECK(esp_now_register_send_cb(espnow_send_cb));
  ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

  // Set primary master key. 
  ESP_ERROR_CHECK(esp_now_set_pmk((uint8_t *)ESPNOW_PMK));

  // Add broadcast peer information to peer list. 
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

   Initialize sending parameters. 
  send_param = malloc(sizeof(espnow_send_param_t));
  memset(send_param, 0, sizeof(espnow_send_param_t));
  if (send_param == NULL) {
    ESP_LOGE(TAG, "Malloc send parameter fail");
    vSemaphoreDelete(s_espnow_queue);
    esp_now_deinit();
    return ESP_FAIL;
  }

  send_param->state = 0;
  send_param->magic = esp_random();
  send_param->len = ESPNOW_SEND_LEN;
  send_param->buffer = malloc(ESPNOW_SEND_LEN);
  if (send_param->buffer == NULL) {
    ESP_LOGE(TAG, "Malloc send buffer fail");
    free(send_param);
    vSemaphoreDelete(s_espnow_queue);
    esp_now_deinit();
    return ESP_FAIL;
  }
  memcpy(send_param->dest_mac, s_broadcast_mac, ESP_NOW_ETH_ALEN);
  espnow_data_prepare(send_param);

  xTaskCreate(espnow_task_send, "espnow_task_send", 2048, send_param, 4, NULL);
  // xTaskCreate(espnow_task_listen, "espnow_task_listen", 2048, send_param, 4,
  // NULL);
  //  xTaskCreate(espnow_task_test, "espnow_task_test", 2048,
  //  send_param, 4, NULL);

  ESP_LOGI(TAG, "Exiting espnow init");
  return ESP_OK;
}
*/
// Some debug code:

/*
esp_err_t esperror =
    esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len);
char *esperrorchar = esp_err_to_name(esperror);
ESP_LOGI(TAG, "esperror: %s", esperrorchar);
*/

// ESP_LOGI(TAG, "Mac test: " MACSTR "", MAC2STR(send_param->dest_mac));

/*
char message[] = "esp message 123";
  int array_size_chars = sizeof(message) / sizeof(message[0]);
  uint8_t array_bytes[array_size_chars];

  for (int i = 0; i < array_size_chars; i++) {
    array_bytes[i] = (uint8_t)message[i];
  }
  int array_size_bytes = sizeof(array_bytes);

  espnow_send_param_t *send_param = espnow_data_create(s_broadcast_mac,
array_bytes, array_size_bytes);

*/

/*

  char message[] = "broadcast jest wysylane 111";
  int array_size_chars = sizeof(message) / sizeof(message[0]);
  uint8_t array_bytes[array_size_chars];

  for (int i = 0; i < array_size_chars; i++) {
    array_bytes[i] = (uint8_t)message[i];
  }

  espnow_send_param_t *send_param =
      espnow_data_create(s_broadcast_mac, array_bytes, array_size_chars);

*/