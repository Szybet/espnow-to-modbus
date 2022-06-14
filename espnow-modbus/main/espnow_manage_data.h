#ifndef ESPNOW_MANAGE_DATA_H
#define ESPNOW_MANAGE_DATA_H

#include "main_settings.h"
#include "espnow_manage_data.c"

void espnow_data_prepare(espnow_send_param_t *send_param, uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN], uint16_t s_espnow_seq[ESPNOW_DATA_MAX]);

static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);

static void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len);







#endif
