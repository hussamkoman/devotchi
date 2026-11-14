#include "Storage.hpp"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace devotchi::storage {

namespace {
constexpr const char* TAG       = "Storage";
constexpr const char* NAMESPACE = "devotchi";

constexpr const char* K_WIFI_SSID = "wifi_ssid";
constexpr const char* K_WIFI_PSK  = "wifi_psk";
constexpr const char* K_SERVER    = "server_url";
constexpr const char* K_TOKEN     = "token";
constexpr const char* K_ROOM      = "room_id";
constexpr const char* K_PAIR      = "pair_code";

std::optional<std::string> read_str(const char* key) {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) return std::nullopt;

    size_t len = 0;
    esp_err_t err = nvs_get_str(h, key, nullptr, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(h);
        return std::nullopt;
    }

    std::string out(len, '\0');
    err = nvs_get_str(h, key, out.data(), &len);
    nvs_close(h);
    if (err != ESP_OK) return std::nullopt;

    if (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

bool write_str(const char* key, std::string_view value) {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return false;

    std::string nul_term(value);
    esp_err_t err = nvs_set_str(h, key, nul_term.c_str());
    if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    return err == ESP_OK;
}
}  // namespace

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

std::optional<WifiCredentials> Storage::wifi_creds() const {
    auto ssid = read_str(K_WIFI_SSID);
    auto psk  = read_str(K_WIFI_PSK);
    if (!ssid) return std::nullopt;
    return WifiCredentials{*ssid, psk.value_or("")};
}

void Storage::set_wifi_creds(const WifiCredentials& creds) {
    write_str(K_WIFI_SSID, creds.ssid);
    write_str(K_WIFI_PSK,  creds.psk);
}

std::optional<std::string> Storage::server_url() const            { return read_str(K_SERVER); }
void Storage::set_server_url(std::string_view url)                { write_str(K_SERVER, url); }

std::optional<std::string> Storage::token() const                 { return read_str(K_TOKEN); }
void Storage::set_token(std::string_view t)                       { write_str(K_TOKEN, t); }

std::optional<std::string> Storage::pairing_code() const          { return read_str(K_PAIR); }
void Storage::set_pairing_code(std::string_view code)             { write_str(K_PAIR, code); }

std::optional<std::string> Storage::room_id() const               { return read_str(K_ROOM); }
void Storage::set_room_id(std::string_view room)                  { write_str(K_ROOM, room); }

void Storage::set_token_and_room(std::string_view t, std::string_view room) {
    write_str(K_TOKEN, t);
    write_str(K_ROOM,  room);
}

void Storage::clear() {
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_erase_all(h);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "NVS namespace cleared");
}

}  // namespace devotchi::storage
