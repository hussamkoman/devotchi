#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace devotchi::pairing {

struct ClaimResult {
    std::string token;
    std::string room_id;
};

/** Synchronous HTTP POST to /api/pair/claim. Blocks for up to network::HTTP_TIMEOUT_MS. */
std::optional<ClaimResult> claim(std::string_view code);

}  // namespace devotchi::pairing
