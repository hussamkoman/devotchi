#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace devotchi::storage {

struct WifiCredentials {
    std::string ssid;
    std::string psk;
};

class Storage {
public:
    void init();

    std::optional<WifiCredentials> wifi_creds() const;
    void set_wifi_creds(const WifiCredentials& creds);

    std::optional<std::string> server_url() const;
    void set_server_url(std::string_view url);

    std::optional<std::string> token() const;
    void set_token(std::string_view token);

    std::optional<std::string> pairing_code() const;
    void set_pairing_code(std::string_view code);

    std::optional<std::string> room_id() const;
    void set_room_id(std::string_view room);

    void set_token_and_room(std::string_view token, std::string_view room);

    void clear();
};

}  // namespace devotchi::storage
