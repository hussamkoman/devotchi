#include "net_priv.h"

#include "sdkconfig.h"

#if CONFIG_DEVOTCHI_TLS_EXTRA_CA
/* Embedded by CMake from CONFIG_DEVOTCHI_TLS_EXTRA_CA_PATH; the TEXT embedding adds the terminator. */
extern const char extra_ca_pem_start[] asm("_binary_extra_ca_pem_start");
#else
#include "esp_crt_bundle.h"
#endif

void tls_cfg_apply_ws(esp_websocket_client_config_t *cfg)
{
#if CONFIG_DEVOTCHI_TLS_EXTRA_CA
    cfg->cert_pem = extra_ca_pem_start;
#else
    cfg->crt_bundle_attach = esp_crt_bundle_attach;
#endif
}

void tls_cfg_apply_http(esp_http_client_config_t *cfg)
{
#if CONFIG_DEVOTCHI_TLS_EXTRA_CA
    cfg->cert_pem = extra_ca_pem_start;
#else
    cfg->crt_bundle_attach = esp_crt_bundle_attach;
#endif
}
