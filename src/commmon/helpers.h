#pragma once

// -----------------------------
// helpers
// -----------------------------

static int atoi_safe(const char* s, int def = -1) {
    return (s && *s) ? std::atoi(s) : def;
}
