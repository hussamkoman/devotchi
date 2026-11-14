#pragma once

#include "net.h"

#define WIFI_SSID_SIZE  33
#define WIFI_IP_SIZE    16

typedef struct {
    bool connected;
    char ssid[WIFI_SSID_SIZE];
    char ip[WIFI_IP_SIZE];
    int rssi;
} wifi_status_t;

/**
 * Brings up the network stack and the WiFi driver in station mode without
 * connecting. use_nvs false keeps the driver away from NVS (degraded storage).
 * A failure leaves the device without WiFi; it is reported, not fatal.
 */
esp_err_t wifi_init(net_event_cb_t cb, bool use_nvs);

/**
 * Connects with the given credentials, dropping a current connection first.
 * The result arrives as NET_EV_WIFI_GOT_IP or NET_EV_WIFI_DISCONNECTED; the
 * driver does not retry on its own.
 */
esp_err_t wifi_connect(const char *ssid, const char *psk);

/** Safe from any task. */
void wifi_get_status(wifi_status_t *out);

/** Factory reset: clears the WiFi driver's own stored configuration (esp_wifi_restore). */
esp_err_t wifi_restore(void);
