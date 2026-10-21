#pragma once

// Sent to ThingsBoard as current_fw_title / current_fw_version. An OTA package is only
// installed when its title matches FW_TITLE and its version differs from FW_VERSION,
// both compared byte-for-byte. Bump FW_VERSION on every release pushed through OTA.
constexpr const char *FW_TITLE = "prayer-timer";
constexpr const char *FW_VERSION = "1.1.3";
