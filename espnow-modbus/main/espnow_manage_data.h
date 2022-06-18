#ifndef ESPNOW_MANAGE_DATA_H
#define ESPNOW_MANAGE_DATA_H

#include "main_settings.h"

void espnow_data_prepare(espnow_send_param_t *send_param);
int espnow_data_parse(uint8_t *data, uint16_t data_len, uint8_t *state, uint16_t *seq, int *magic);
espnow_send_param_t *espnow_data_create(uint8_t mac[ESP_NOW_ETH_ALEN], uint8_t* array, int array_length);
void espnow_addpeer(uint8_t *mac);
void print_mac(const unsigned char *mac);

void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status);
void espnow_recv_cb(const uint8_t *mac_addr, const uint8_t *data, int len);

#endif