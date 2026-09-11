#include "automation/interlock.hpp"

#include "esp_log.h"
#include "io/input_driver.hpp"
#include "isobus/vt_app.hpp"

namespace automation::interlock {

namespace {
constexpr const char* kTag = "interlock";
bool g_disabled[9] = {};  // index 1-8, [0] unused
}  // namespace

void init() {
    for (int ch = 1; ch <= 8; ++ch) {
        g_disabled[ch] = false;
    }
}

void update() {
    io::input_driver::update();

    for (int ch = 1; ch <= 8; ++ch) {
        bool di_active = io::input_driver::read_debounced(static_cast<uint8_t>(ch));
        if (di_active == g_disabled[ch]) {
            continue;
        }
        ESP_LOGI(kTag, "DI%d -> %s: channel %d %s", ch, di_active ? "active" : "inactive", ch,
                 di_active ? "disabled" : "re-enabled (stays off until commanded on)");
        g_disabled[ch] = di_active;
        iso::vt_app::set_interlock_state(ch, di_active);
    }
}

bool is_disabled(int channel) {
    if (channel < 1 || channel > 8) {
        return false;
    }
    return g_disabled[channel];
}

}  // namespace automation::interlock
