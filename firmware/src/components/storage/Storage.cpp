#include "Storage.hpp"

#include "esp_log.h"
#include "nvs_flash.h"

namespace devotchi::storage {

namespace {
constexpr const char* TAG = "Storage";
}

void Storage::init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition needs erase, doing it now");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    } else {
        ESP_ERROR_CHECK(err);
    }
    ESP_LOGI(TAG, "NVS initialized");
}

std::optional<WifiCredentials> Storage::wifi_creds() const             { return std::nullopt; }
void Storage::set_wifi_creds(const WifiCredentials&)                   {}
std::optional<std::string>     Storage::server_url() const             { return std::nullopt; }
void Storage::set_server_url(std::string_view)                         {}
std::optional<std::string>     Storage::token() const                  { return std::nullopt; }
void Storage::set_token(std::string_view)                              {}
std::optional<std::string>     Storage::pairing_code() const           { return std::nullopt; }
void Storage::set_pairing_code(std::string_view)                       {}

}  // namespace devotchi::storage
