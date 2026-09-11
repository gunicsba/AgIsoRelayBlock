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
constexpr size_t kGeneratedPasswordLen = 12;
constexpr char kPasswordAlphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
// WPA2-PSK's actual protocol minimum (a hard limit, not a policy choice) --
// enforced in set_password() below, independent of whatever a caller (the
// VT panel) already validated.
constexpr size_t kMinPasswordLen = 8;

std::string g_ssid;
std::string g_password;
bool g_enabled = false;

void generate_password(std::string& out) {
    constexpr size_t kAlphabetLen = sizeof(kPasswordAlphabet) - 1;  // exclude the trailing '\0'
    out.resize(kGeneratedPasswordLen);
    for (char& c : out) {
        c = kPasswordAlphabet[esp_random() % kAlphabetLen];
    }
}

// Generated once on first boot, then persisted -- not derived from the
// MAC or any other fixed value (requirement N7: no fixed/shared default).
// The serial log and the VT panel (docs/vt-ui-design.md's WiFi panel,
// get_password() below) are the two ways to read it back; no factory-
// reset path yet (see docs/architecture.md's open questions, deferred to
// Phase 8).
std::string load_or_generate_password() {
    std::string password;
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(kTag, "nvs_open failed (%s) -- using an unsaved password, will change next boot", esp_err_to_name(err));
        generate_password(password);
        return password;
    }

    size_t required_size = 0;
    err = nvs_get_str(handle, kNvsPasswordKey, nullptr, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        generate_password(password);
        esp_err_t set_err = nvs_set_str(handle, kNvsPasswordKey, password.c_str());
        if (ESP_OK == set_err) {
            set_err = nvs_commit(handle);
        }
        if (ESP_OK != set_err) {
            ESP_LOGE(kTag, "Failed to persist generated AP password (%s) -- will regenerate next boot", esp_err_to_name(set_err));
        }
    } else if (ESP_OK == err) {
        password.resize(required_size - 1);  // required_size includes the trailing '\0'
        nvs_get_str(handle, kNvsPasswordKey, password.data(), &required_size);
    } else {
        ESP_LOGE(kTag, "nvs_get_str failed (%s) -- using an unsaved password", esp_err_to_name(err));
        generate_password(password);
    }
    nvs_close(handle);
    return password;
}

bool persist_password(const std::string& password) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &handle);
    if (ESP_OK != err) {
        ESP_LOGE(kTag, "nvs_open failed (%s), new password won't survive a reboot", esp_err_to_name(err));
        return false;
    }
    err = nvs_set_str(handle, kNvsPasswordKey, password.c_str());
    if (ESP_OK == err) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);
    if (ESP_OK != err) {
        ESP_LOGE(kTag, "Failed to persist new AP password (%s), won't survive a reboot", esp_err_to_name(err));
        return false;
    }
    return true;
}

void apply_ap_config() {
    wifi_config_t wifi_config = {};
    std::memcpy(wifi_config.ap.ssid, g_ssid.data(), g_ssid.size());
    wifi_config.ap.ssid_len = static_cast<uint8_t>(g_ssid.size());
    std::strncpy(reinterpret_cast<char*>(wifi_config.ap.password), g_password.c_str(), sizeof(wifi_config.ap.password) - 1);
    wifi_config.ap.channel = 6;
    wifi_config.ap.max_connection = 4;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.ap.pmf_cfg.required = false;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
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
    char ssid_buf[33];
    snprintf(ssid_buf, sizeof(ssid_buf), "AgIsoBlock-%02X%02X", mac[4], mac[5]);
    g_ssid = ssid_buf;
    g_password = load_or_generate_password();

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    apply_ap_config();
    ESP_ERROR_CHECK(esp_wifi_start());
    g_enabled = true;

    ESP_LOGI(kTag, "SoftAP up: SSID=\"%s\" password=\"%s\" -- connect, then browse to http://192.168.4.1/",
             g_ssid.c_str(), g_password.c_str());
}

bool is_enabled() {
    return g_enabled;
}

void set_enabled(bool enabled) {
    if (enabled == g_enabled) {
        return;
    }
    esp_err_t err = enabled ? esp_wifi_start() : esp_wifi_stop();
    if (ESP_OK != err) {
        ESP_LOGE(kTag, "esp_wifi_%s failed: %s", enabled ? "start" : "stop", esp_err_to_name(err));
        return;
    }
    g_enabled = enabled;
    ESP_LOGI(kTag, "SoftAP %s from the VT panel", enabled ? "re-enabled" : "disabled");
}

std::string get_ssid() {
    return g_ssid;
}

std::string get_password() {
    return g_password;
}

bool set_password(const std::string& password) {
    if (password.size() < kMinPasswordLen) {
        ESP_LOGW(kTag, "Rejected new AP password: %u chars, WPA2-PSK requires at least %u",
                 static_cast<unsigned>(password.size()), static_cast<unsigned>(kMinPasswordLen));
        return false;
    }
    g_password = password;
    persist_password(g_password);  // best-effort -- still applies below even if this fails, just won't survive a reboot
    if (g_enabled) {
        apply_ap_config();
    }
    ESP_LOGI(kTag, "AP password changed from the VT panel");
    return true;
}

uint8_t get_connected_client_count() {
    if (!g_enabled) {
        return 0;
    }
    wifi_sta_list_t sta_list = {};
    if (ESP_OK != esp_wifi_ap_get_sta_list(&sta_list)) {
        return 0;
    }
    return static_cast<uint8_t>(sta_list.num);
}

}  // namespace net::wifi_ap
