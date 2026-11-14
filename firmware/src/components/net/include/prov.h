#pragma once

#include "net.h"

#define PROV_SERVICE_NAME_SIZE  32
#define PROV_POP_LEN            8
#define PROV_POP_SIZE           (PROV_POP_LEN + 1)
#define PROV_SSID_SIZE          33
#define PROV_PSK_SIZE           65

typedef struct {
    char service_name[PROV_SERVICE_NAME_SIZE];
    /** Proof-of-possession of this session, for the screen only; never logged. */
    char pop[PROV_POP_SIZE];
} prov_info_t;

/**
 * Starts BLE provisioning; wifi_init() must have succeeded. While it runs the
 * provisioning manager owns the station's connection attempts.
 */
esp_err_t prov_start(net_event_cb_t cb, prov_info_t *out);

/** Copies the credentials the phone sent; false when none were received. */
bool prov_get_credentials(char ssid[PROV_SSID_SIZE], char psk[PROV_PSK_SIZE]);

/** Lets the phone send other credentials after NET_EV_PROV_CRED_FAIL. */
void prov_retry_after_failure(void);

/** Releases the provisioning manager after NET_EV_PROV_END. */
void prov_finish(void);

/** Returns the Bluetooth controller's memory to the heap on a boot that does not provision. */
void prov_release_bt_memory(void);
