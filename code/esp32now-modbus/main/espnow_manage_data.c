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

#include "driver/gpio.h"

// Manage / Convert espnow data

espnow_send *espnow_data_create(uint8_t mac[ESP_NOW_ETH_ALEN], uint8_t *array,
                                int array_length) {
  espnow_send *send_param;
  send_param = malloc(sizeof(espnow_send));
  memset(send_param, 0, sizeof(espnow_send));
  if (send_param == NULL) {
    ESP_LOGE(TAG, "Malloc send parameter fail");
    vSemaphoreDelete(s_espnow_queue);
    esp_now_deinit();
    // return ESP_FAIL;
  }

  // ESPNOW_SEND_LEN = 10
  // ESP_NOW_MAX_DATA_LEN = 250 bytes
  // https://docs.espressif.com/projects/esp-idf/en/v4.4.1/esp32/api-reference/network/esp_now.html?highlight=esp_now_send#_CPPv412esp_now_sendPK7uint8_tPK7uint8_t6size_t
  // https://flaviocopes.com/c-array-length/
  if (array_length > ESP_NOW_MAX_DATA_LEN) {
    ESP_LOGE(TAG, "Array lenght to long");
  }

  // https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html
  // Length: The length is the total length of Organization Identifier, Type,
  // Version and Body.

  if(array_length == 0)
  {
    ESP_LOGI(TAG, "Lenght to send is 0, esp now doesnt like it");
  }

  send_param->len = array_length; // hmmmmmmm
  send_param->buffer = malloc(array_length);
  if (send_param->buffer == NULL) {
    ESP_LOGE(TAG, "Malloc send buffer fail");
    free(send_param);
    vSemaphoreDelete(s_espnow_queue);
    esp_now_deinit();
    // return ESP_FAIL;
  }
  memcpy(send_param->buffer, array, array_length);
  memcpy(send_param->dest_mac, mac, ESP_NOW_ETH_ALEN);
  return send_param;
}

void print_mac(const unsigned char *mac) {
  printf("%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
         mac[4], mac[5]);
}

// Manage / Convert data end

// Receive / Send espnow data
/* ESPNOW sending or receiving callback function is called in WiFi task.
 * Users should not do lengthy operations from this task. Instead, post
 * necessary data to a queue and handle it from a lower priority task. */
void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status) {
  espnow_event_t evt;
  espnow_event_send_cb_t *send_cb = &evt.info.send_cb;

  if (mac_addr == NULL) {
    ESP_LOGE(TAG, "Send cb arg error");
    return;
  }

  evt.id = ESPNOW_SEND_CB;
  memcpy(send_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
  if (xQueueOverwrite(s_espnow_queue, &evt) != pdTRUE) {
    ESP_LOGW(TAG, "Send send queue fail");
  }
}

typedef struct {
    uint16_t frame_head;
    uint16_t duration;
    uint8_t destination_address[6];
    uint8_t source_address[6];
    uint8_t broadcast_address[6];
    uint16_t sequence_control;

    uint8_t category_code;
    uint8_t organization_identifier[3]; // 0x18fe34
    uint8_t random_values[4];
    struct {
        uint8_t element_id;                 // 0xdd
        uint8_t lenght;                     //
        uint8_t organization_identifier[3]; // 0x18fe34
        uint8_t type;                       // 4
        uint8_t version;
        uint8_t body[0];
    } vendor_specific_content;
} __attribute__((packed)) espnow_frame_format_t;

void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len) {
  wifi_promiscuous_pkt_t *promiscuous_pkt = (wifi_promiscuous_pkt_t *)(data - sizeof(wifi_pkt_rx_ctrl_t) - sizeof(espnow_frame_format_t));
  wifi_pkt_rx_ctrl_t *rx_ctrl = &promiscuous_pkt->rx_ctrl;



  ESP_LOGI(TAG, "rssi: %d", rx_ctrl->rssi);

  if(rx_ctrl->rssi > -80) {
    set_diodes(4);
  } else if(rx_ctrl->rssi > -90) {
    set_diodes(3);
  } else if(rx_ctrl->rssi > -100) {
    set_diodes(2);
  } else if(rx_ctrl->rssi > -110) {
    set_diodes(1);
  } else {
    set_diodes(0);
  }


  espnow_event_t evt;
  espnow_event_recv_cb_t *recv_cb = &evt.info.recv_cb;

  if (mac_addr == NULL || data == NULL || len <= 0) {
    ESP_LOGE(TAG, "Receive cb arg error");
    return;
  }

  evt.id = ESPNOW_RECV_CB;
  memcpy(recv_cb->mac_addr, mac_addr, ESP_NOW_ETH_ALEN);
  recv_cb->data = malloc(len);
  if (recv_cb->data == NULL) {
    ESP_LOGE(TAG, "Malloc receive data fail");
    return;
  }
  memcpy(recv_cb->data, data, len);
  recv_cb->data_len = len;
  if (xQueueOverwrite(s_espnow_queue, &evt) != pdTRUE) {
    ESP_LOGW(TAG, "Send receive queue fail");
    free(recv_cb->data);
  }
}

void espnow_addpeer(uint8_t *mac) {
  if (esp_now_is_peer_exist(mac) == false) {
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
    memcpy(peer->peer_addr, mac, ESP_NOW_ETH_ALEN);
    ESP_ERROR_CHECK(esp_now_add_peer(peer));
    free(peer);
  }
}

void espnow_send_smarter(espnow_send *data) {
  ESP_LOGI(TAG, "Sending espnow to: " MACSTR " with length: %d", MAC2STR(data->dest_mac), data->len);

  esp_now_send(data->dest_mac, data->buffer, data->len);
  free(data->buffer);
}

void set_diodes(int count) {
  int x1 = 1;
  int x2 = 0;
  if(count == 0) {
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_33, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_25, x2));
  } else if(count == 1) {
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_33, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_25, x1));
  } else if(count == 2) {
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_33, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, x1));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_25, x1));
  } else if(count == 3) {
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_33, x2));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, x1));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, x1));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_25, x1));
  } else {
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_33, x1));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_27, x1));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_26, x1));
      ESP_ERROR_CHECK(gpio_set_level(GPIO_NUM_25, x1));
  }
}

// old
/*

// this function tweaks buf inside send_param
void espnow_data_prepare(espnow_send *send_param) {
  espnow_data_t *buf = (espnow_data_t *)send_param->buffer;

  ESP_LOGI(TAG, "send param lenght: %d", send_param->len);
  ESP_LOGI(TAG, "espnow_data_t size: %d", sizeof(espnow_data_t));

  // this is stupid becouse in espnow_data_t is only a pointer to the data, and
  // always has 10
  // assert(send_param->len >= sizeof(espnow_data_t));

  buf->type = IS_BROADCAST_ADDR(send_param->dest_mac) ? ESPNOW_DATA_BROADCAST
                                                      : ESPNOW_DATA_UNICAST;
  buf->state = send_param->state;
  buf->seq_num = s_espnow_seq[buf->type]++;
  buf->crc = 0;
  buf->magic = send_param->magic;

  // esp_fill_random(buf->payload, send_param->len - sizeof(espnow_data_t));
  buf->crc = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, send_param->len);
}

int espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state,
                      uint16_t *seq, int *magic) {
  espnow_data_t *buf = (espnow_data_t *)data;
  uint16_t crc, crc_cal = 0;

  if (data_len < sizeof(espnow_data_t)) {
    ESP_LOGE(TAG, "Receive ESPNOW data too short, len:%d", data_len);
    return -1;
  }

  *state = buf->state;
  *seq = buf->seq_num;
  *magic = buf->magic;
  crc = buf->crc;
  buf->crc = 0;
  crc_cal = esp_crc16_le(UINT16_MAX, (uint8_t const *)buf, data_len);

  if (crc_cal == crc) {
    return buf->type;
  }

  ESP_LOGE(TAG, "ESP32 crc checksum failed");
  return -1;
}

espnow_send *espnow_data_create(uint8_t mac[ESP_NOW_ETH_ALEN],
                                        uint8_t *array, int array_size) {
  espnow_send *send_param;
  send_param = malloc(sizeof(espnow_send));
  memset(send_param, 0, sizeof(espnow_send));
  if (send_param == NULL) {
    ESP_LOGE(TAG, "Malloc send parameter fail");
    vSemaphoreDelete(s_espnow_queue);
    esp_now_deinit();
    // return ESP_FAIL;
  }

  send_param->state = 0;
  send_param->magic = esp_random();
  // ESPNOW_SEND_LEN = 10
  // ESP_NOW_MAX_DATA_LEN = 250 bytes
  //
https://docs.espressif.com/projects/esp-idf/en/v4.4.1/esp32/api-reference/network/esp_now.html?highlight=esp_now_send#_CPPv412esp_now_sendPK7uint8_tPK7uint8_t6size_t

  // https://flaviocopes.com/c-array-length/
  if (array_size > ESP_NOW_MAX_DATA_LEN) {
    ESP_LOGE(TAG, "Array lenght to long");
  }

  //
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/network/esp_now.html
  // Length: The length is the total length of Organization Identifier, Type,
  // Version and Body.
  //send_param->len = 3 + 1 + 1 + array_size; // this doesnt work.
  send_param->len = array_size + 1 + 1 + 2 + 2 + 4;
  send_param->buffer = malloc(array_size);  // ???
  if (send_param->buffer == NULL) {
    ESP_LOGE(TAG, "Malloc send buffer fail");
    free(send_param);
    vSemaphoreDelete(s_espnow_queue);
    esp_now_deinit();
    // return ESP_FAIL;
  }

  memcpy(send_param->buffer, array, send_param->len);

  memcpy(send_param->dest_mac, mac, ESP_NOW_ETH_ALEN);

  espnow_data_prepare(send_param);
  return send_param;
}

*/
