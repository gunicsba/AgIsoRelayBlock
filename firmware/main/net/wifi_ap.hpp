#pragma once

// Phase 7 (docs/roadmap.md#phase-7--wifi-ap--ota, docs/architecture.md#wifi-ap--ota-planned):
// always-on SoftAP, independent of Ethernet/station state, so there's
// always a way to reach the device for local config/OTA -- even freshly
// unboxed or mounted somewhere without a network drop.
namespace net::wifi_ap {

// Initializes NVS (required by the WiFi driver, and where the AP password
// is persisted), brings up a SoftAP named "AgIsoBlock-XXXX" (XXXX = the
// last 2 bytes of the station MAC), and logs the SSID/password once at
// INFO level. The password is generated once on first boot and persisted
// to NVS from then on (never a fixed/shared default -- requirement N7);
// the serial log is currently the only way to retrieve it post-generation
// -- see the comment in wifi_ap.cpp for why, and docs/architecture.md's
// open questions for the not-yet-decided factory-reset story (Phase 8).
// Call once, early in app_main -- must run before net::web_server::init().
void init();

}  // namespace net::wifi_ap
