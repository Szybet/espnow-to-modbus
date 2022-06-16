#ifndef DEBUG_H
#define DEBUG_H

#include "main_settings.h"
#include "espnow_manage_data.h"

void espnow_task_listen(void *pvParameter);
esp_err_t espnow_init_listen(void);


#endif