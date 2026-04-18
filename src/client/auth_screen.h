#pragma once

#include "net_client.h"

#include <optional>
#include <string>

namespace authui {

struct AuthResult {
    std::string username;
};

// Renders the pre-game login screen and drives the authentication round-trip
// with the server. Returns the chosen identity on success, nullopt if the
// user closed the window before authenticating.
std::optional<AuthResult> runAuthScreen(netc::NetClient& net);

} // namespace authui
