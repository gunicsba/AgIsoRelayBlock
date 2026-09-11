#pragma once

// Phase 7 (docs/roadmap.md#phase-7--wifi-ap--ota): a minimal local web UI
// reachable over the SoftAP (net/wifi_ap.hpp) or Ethernet -- a status/
// control page mirroring what the VT's Data Mask shows (read + toggle
// each relay, see DI interlock status), and a firmware upload form for
// OTA. Deliberately not a general-purpose config UI (see docs/roadmap.md
// Phase 9 for the richer version that's explicitly out of scope for now).
namespace net::web_server {

// Starts the HTTP server and registers all routes. Call once, after both
// net::wifi_ap::init() (network must be up) and iso::vt_app::init() (the
// relay-control routes call into iso::vt_app::set_relay_remote(), and
// reads go through io::relay_driver / automation::interlock directly).
void init();

}  // namespace net::web_server
