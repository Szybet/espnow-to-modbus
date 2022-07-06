#ifndef UART_DATA_H
#define UART_DATA_H

#include <stdint.h>

#include "main_settings.h"

void uart_send_data(uint8_t *data_to_send, int data_size);

size_t uart_receive_data(uint8_t *buffer, size_t buffer_size, int packet_timeout, int byte_timeout);

#endif