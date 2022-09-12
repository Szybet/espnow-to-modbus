#ifndef ESPNOW_MANAGE_DATA_H
#define ESPNOW_MANAGE_DATA_H

#include "main_settings.h"

void espnow_send_smarter(espnow_send* data);
espnow_send* espnow_data_create(uint8_t mac[ESP_NOW_ETH_ALEN], uint8_t* array, int array_length);
void espnow_addpeer(uint8_t *mac);
void print_mac(const unsigned char *mac);

void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len);

// 1-4
void set_diodes(int count);

#endif
