#include "net/wifi_ap.hpp"

#include <cstdio>
#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace net::wifi_ap {

namespace {
constexpr const char* kTag = "wifi_ap";
constexpr const char* kNvsNamespace = "wifi_ap";
constexpr const char* kNvsPasswordKey = "password";
// 12 chars from a 58-symbol alphabet is ~70 bits of entropy -- comfortably
// more than WPA2-PSK's practical brute-force resistance needs for a local
// AP, while still short enough to type by hand off a serial log. Excludes
// visually-ambiguous characters (0/O, 1/l/I) since a human has to type it.
constexpr size_t kPasswordLen = 12;
constexpr char kPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";

void generate_password(char* out, size_t len) {
    constexpr size_t kAlphabetLen = sizeof(kPasswordAlphabet) - 1;  // exclude the trailing '\0'
    for (size_t i = 0; i < len; ++i) {
        out[i] = kPasswordAlphabet[esp_random() % kAlphabetLen];
    }
    out[len] = '\0';
}

// Generated once on first boot, then persisted -- not derived from the
// MAC or any other fixed value (requirement N7: no fixed/shared default).
// The serial log is currently the only way to read it back: there's no
// VT/web display for it yet (chicken-and-egg for the web UI -- you need
// the password to reach the page that would show it), and no factory-
// reset path yet either (see docs/architecture.md's open questions,
// explicitly deferred to Phase 8). Acceptable for now since reaching the
// device to read this log already requires a USB connection during setup.
void load_or_generate_password(char* out, size_t out_size) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_open failed (%s) -- using an unsaved password, will change next boot", esp_err_to_name(err));
        generate_password(out, kPasswordLen);
        return;
    }

    size_t required_size = out_size;
    err = nvs_get_str(handle, kNvsPasswordKey, out, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        generate_password(out, kPasswordLen);
        esp_err_t set_err = nvs_set_str(handle, kNvsPasswordKey, out);
        if (ESP_OK == set_err) {
            set_err = nvs_commit(handle);
        }
        if (ESP_OK != set_err) {
            ESP_LOGE(kTag, "Failed to persist generated AP password (%s) -- will regenerate next boot", esp_err_to_name(set_err));
        }
    } else if (ESP_OK != err) {
        ESP_LOGE(kTag, "nvs_get_str failed (%s) -- using an unsaved password", esp_err_to_name(err));
        generate_password(out, kPasswordLen);
    }
    nvs_close(handle);
}
}  // namespace

void init() {
    esp_err_t err = nvs_flash_init();
    if (ESP_ERR_NVS_NO_FREE_PAGES == err || ESP_ERR_NVS_NEW_VERSION_FOUND == err) {
        // Layout changed (or first-ever boot on fresh flash) -- nothing
        // meaningful survives this either way, so erase and retry rather
        // than leaving NVS (and everything that depends on it, including
        // the AP password below) permanently broken.
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));

    // docs/architecture.md#wifi-ap--ota-planned: SSID from the last 2
    // bytes of the *station* MAC even though we only ever run AP mode --
    // deterministic per unit, matches the MAC already printed on the
    // module, no separate AP-mode MAC to look up.
    uint8_t mac[6] = {};
    ESP_ERROR_CHECK(esp_wifi_get_mac(WIFI_IF_STA, mac));

    char password[kPasswordLen + 1] = {};
    load_or_generate_password(password, sizeof(password));

    wifi_config_t wifi_config = {};
    int ssid_len = snprintf(reinterpret_cast<char*>(wifi_config.ap.ssid), sizeof(wifi_config.ap.ssid),
                             "AgIsoBlock-%02X%02X", mac[4], mac[5]);
    wifi_config.ap.ssid_len = static_cast<uint8_t>(ssid_len);
    std::strncpy(reinterpret_cast<char*>(wifi_config.ap.password), password, sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.channel = 6;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(kTag, "SoftAP up: SSID=\"%s\" password=\"%s\" -- connect, then browse to http://192.168.4.1/",
             wifi_config.ap.ssid, password);
}

}  // namespace net::wifi_ap
