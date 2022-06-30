#ifndef UART_DATA_H
#define UART_DATA_H

#include <stdint.h>

#include "main_settings.h"

void uart_send_data(uint8_t* data_to_send, int data_size);

uint8_t* uart_receive_data();


#endif