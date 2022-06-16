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
// range 10 250

static void espnow_task_test(void *pvParameter) {
  espnow_event_t evt;
  uint8_t recv_state = 0;
  uint16_t recv_seq = 0;
  int recv_magic = 0;
  bool is_broadcast = false;
  int ret;

  vTaskDelay(5000 / portTICK_RATE_MS);
  ESP_LOGI(TAG, "Start sending broadcast data");

  /* Start sending broadcast ESPNOW data. */
  espnow_send_param_t *send_param = (espnow_send_param_t *)pvParameter;
  if (esp_now_send(send_param->dest_mac, send_param->buffer, send_param->len) !=
      ESP_OK) {
    ESP_LOGE(TAG, "Send error");
    espnow_deinit_func(send_param);
    vTaskDelete(NULL);
  }

  while (xQueueReceive(s_espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
    switch (evt.id) {
    case ESPNOW_SEND_CB: {
      espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
      is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

      ESP_LOGD(TAG, "Send data to " MACSTR ", status1: %d",
               MAC2STR(send_cb->mac_addr), send_cb->status);

      if (is_broadcast && (send_param->broadcast == false)) {
        break;
      }

      if (!is_broadcast) {
        send_param->count--;
        if (send_param->count == 0) {
          ESP_LOGI(TAG, "Send done");
          espnow_deinit_func(send_param);
          vTaskDelete(NULL);
        }
      }

      /* Delay a while before sending the next data. */
      if (send_param->delay > 0) {
        vTaskDelay(send_param->delay / portTICK_RATE_MS);
      }

      ESP_LOGI(TAG, "send data to " MACSTR "", MAC2STR(send_cb->mac_addr));

      memcpy(send_param->dest_mac, send_cb->mac_addr, ESP_NOW_ETH_ALEN);
      espnow_data_prepare(send_param);

      /* Send the next data after the previous data is sent. */
      if (esp_now_send(send_param->dest_mac, send_param->buffer,
                       send_param->len) != ESP_OK) {
        ESP_LOGE(TAG, "Send error");
        espnow_deinit_func(send_param);
        vTaskDelete(NULL);
      }
      break;
    }
    case ESPNOW_RECV_CB: {
      espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

      ret = espnow_data_parse(recv_cb->data, recv_cb->data_len, &recv_state,
                              &recv_seq, &recv_magic);
      free(recv_cb->data);
      if (ret == ESPNOW_DATA_BROADCAST) {
        ESP_LOGI(TAG, "Receive %dth broadcast data from: " MACSTR ", len: %d",
                 recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

        /* If MAC address does not exist in peer list, add it to peer list. */
        if (esp_now_is_peer_exist(recv_cb->mac_addr) == false) {
          esp_now_peer_info_t *peer = malloc(sizeof(esp_now_peer_info_t));
          if (peer == NULL) {
            ESP_LOGE(TAG, "Malloc peer information fail");
            espnow_deinit_func(send_param);
            vTaskDelete(NULL);
          }
          memset(peer, 0, sizeof(esp_now_peer_info_t));
          peer->channel = ESPNOW_CHANNEL;
          peer->ifidx = ESPNOW_WIFI_IF;
          peer->encrypt = true;
          memcpy(peer->lmk, ESPNOW_LMK, ESP_NOW_KEY_LEN);
          memcpy(peer->peer_addr, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
          ESP_ERROR_CHECK(esp_now_add_peer(peer));
          free(peer);
        }

        /* Indicates that the device has received broadcast ESPNOW data. */
        if (send_param->state == 0) {
          send_param->state = 1;
        }

        /* If receive broadcast ESPNOW data which indicates that the other
         * device has received broadcast ESPNOW data and the local magic number
         * is bigger than that in the received broadcast ESPNOW data, stop
         * sending broadcast ESPNOW data and start sending unicast ESPNOW data.
         */
        if (recv_state == 1) {
          /* The device which has the bigger magic number sends ESPNOW data, the
           * other one receives ESPNOW data.
           */
          if (send_param->unicast == false && send_param->magic >= recv_magic) {
            ESP_LOGI(TAG, "Start sending unicast data");
            ESP_LOGI(TAG, "send data to " MACSTR "",
                     MAC2STR(recv_cb->mac_addr));

            /* Start sending unicast ESPNOW data. */
            memcpy(send_param->dest_mac, recv_cb->mac_addr, ESP_NOW_ETH_ALEN);
            espnow_data_prepare(send_param);
            if (esp_now_send(send_param->dest_mac, send_param->buffer,
                             send_param->len) != ESP_OK) {
              ESP_LOGE(TAG, "Send error");
              espnow_deinit_func(send_param);
              vTaskDelete(NULL);
            } else {
              send_param->broadcast = false;
              send_param->unicast = true;
            }
          }
        }
      } else if (ret == ESPNOW_DATA_UNICAST) {
        ESP_LOGI(TAG, "Receive %dth unicast data from: " MACSTR ", len: %d",
                 recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len);

        /* If receive unicast ESPNOW data, also stop sending broadcast ESPNOW
         * data. */
        send_param->broadcast = false;
      } else {
        ESP_LOGI(TAG, "Receive error data from: " MACSTR "",
                 MAC2STR(recv_cb->mac_addr));
      }
      break;
    }
    default:
      ESP_LOGE(TAG, "Callback type error: %d", evt.id);
      break;
    }
  }
}

void espnow_task_listen(void *pvParameter) {
  espnow_event_t evt;
  uint8_t recv_state = 0;
  uint16_t recv_seq = 0;
  int recv_magic = 0;
  bool is_broadcast = false;
  int ret;

  vTaskDelay(5000 / portTICK_RATE_MS);
  ESP_LOGI(TAG, "Start listening");

  while (xQueueReceive(s_espnow_queue, &evt, portMAX_DELAY) == pdTRUE) {
    switch (evt.id) {
    case ESPNOW_SEND_CB: {
      ESP_LOGI(TAG, "Sending data?");
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

      free(recv_cb->data);
      if (ret == ESPNOW_DATA_BROADCAST) {
        ESP_LOGI(
            TAG,
            "Receive %dth broadcast data from: " MACSTR ", len: %d, crc: %d",
            recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len, crc_cal);
      } else if (ret == ESPNOW_DATA_UNICAST) {
        ESP_LOGI(
            TAG, "Receive %dth unicast data from: " MACSTR ", len: %d, crc: %d",
            recv_seq, MAC2STR(recv_cb->mac_addr), recv_cb->data_len, crc_cal);
      } else {
        ESP_LOGI(TAG, "Receive error data from: " MACSTR "",
                 MAC2STR(recv_cb->mac_addr));
      }
      break;
    }
    default:
      ESP_LOGE(TAG, "Callback type error: %d", evt.id);
      break;
    }
  }
}

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

    send_param->unicast = false;
    send_param->broadcast = true;
    send_param->state = 0;
    send_param->magic = esp_random();
    send_param->count = 1; // here it idicates how much to send them
    send_param->delay = ESPNOW_SEND_DELAY;
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
    while (xQueueReceive(s_espnow_queue, &evt, 5) == pdTRUE) {
      switch (evt.id) {
      case ESPNOW_SEND_CB: {
        espnow_event_send_cb_t *send_cb = &evt.info.send_cb;
        is_broadcast = IS_BROADCAST_ADDR(send_cb->mac_addr);

        ESP_LOGI(TAG, "Send data to " MACSTR ", status: %d",
                 MAC2STR(send_cb->mac_addr), send_cb->status);

        /*if (is_broadcast && (send_param->broadcast == false))
        {
            break;
        }*/

        /* Delay a while before sending the next data. */
        /*if (send_param->delay > 0)
        {
            vTaskDelay(send_param->delay / portTICK_RATE_MS);
        }*/
        break;
      }
      case ESPNOW_RECV_CB: {
        ESP_LOGI(TAG, "Received message?");
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

  /* Initialize sending parameters. */
  send_param = malloc(sizeof(espnow_send_param_t));
  memset(send_param, 0, sizeof(espnow_send_param_t));
  if (send_param == NULL) {
    ESP_LOGE(TAG, "Malloc send parameter fail");
    vSemaphoreDelete(s_espnow_queue);
    esp_now_deinit();
    return ESP_FAIL;
  }

  send_param->unicast = false;
  send_param->broadcast = true;
  send_param->state = 0;
  send_param->magic = esp_random();
  send_param->count = 1; // here it idicates how much to send them
  send_param->delay = ESPNOW_SEND_DELAY;
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
  //  xTaskCreate(espnow_task_test, "espnow_task_test", 2048, send_param, 4,
  //  NULL);

  ESP_LOGI(TAG, "Exiting espnow init");
  return ESP_OK;
}