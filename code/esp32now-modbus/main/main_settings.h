#ifndef MAIN_SETTINGS_H
#define MAIN_SETTINGS_H

#define ESPNOW_WIFI_MODE WIFI_MODE_STA
#define ESPNOW_WIFI_IF ESP_IF_WIFI_STA

#define ESPNOW_QUEUE_SIZE 1

extern uint8_t s_broadcast_mac[ESP_NOW_ETH_ALEN];
extern uint16_t s_espnow_seq[2];

static const char *TAG = "log";

extern xQueueHandle s_espnow_queue;

#define IS_BROADCAST_ADDR(addr) (memcmp(addr, s_broadcast_mac, ESP_NOW_ETH_ALEN) == 0)

#define ESPNOW_MAXDELAY 512

// var begin

#define ESPNOW_PMK "pmk1234567890123"
// ESPNOW primary master to use. The length of ESPNOW primary master must be 16 bytes.
#define ESPNOW_LMK "lmk1234567890123"
// ESPNOW local master to use. The length of ESPNOW local master must be 16 bytes.
#define ESPNOW_CHANNEL 1
// range 0 14
#define ESPNOW_SEND_DELAY 1000
// range 0 65535
// Delay between sending two ESPNOW data, unit: ms.
#define ESPNOW_SEND_LEN 10
// "Send len"
//range 10 250

#define UART_TIMEOUT 200

#define BYTE_LOGS false

typedef enum
{
    ESPNOW_SEND_CB,
    ESPNOW_RECV_CB,
} espnow_event_id_t;

typedef struct
{
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
} espnow_event_send_cb_t;

typedef struct
{
    uint8_t mac_addr[ESP_NOW_ETH_ALEN];
    uint8_t *data;
    int data_len;
} espnow_event_recv_cb_t;

typedef union
{
    espnow_event_send_cb_t send_cb;
    espnow_event_recv_cb_t recv_cb;
} espnow_event_info_t;

/* When ESPNOW sending or receiving callback function is called, post event to ESPNOW task. */
typedef struct
{
    espnow_event_id_t id;
    espnow_event_info_t info;
} espnow_event_t;

enum
{
    ESPNOW_DATA_BROADCAST,
    ESPNOW_DATA_UNICAST,
    ESPNOW_DATA_MAX,
};

/* User defined field of ESPNOW. */
typedef struct
{
    uint8_t payload[0]; // Real payload of ESPNOW data.
} __attribute__((packed)) espnow_data_t;

/* Parameters of sending ESPNOW data. */
typedef struct
{
    int len;                            // Length of ESPNOW data to be sent, unit: byte.
    uint8_t *buffer;                    // Buffer pointing to ESPNOW data.
    uint8_t dest_mac[ESP_NOW_ETH_ALEN]; // MAC address of destination device.
} espnow_send;

void espnow_deinit_func(espnow_send *send_param);

#endif
