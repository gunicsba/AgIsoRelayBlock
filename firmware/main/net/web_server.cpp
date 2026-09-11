#include "net/web_server.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "automation/interlock.hpp"
#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "io/relay_driver.hpp"
#include "isobus/vt_app.hpp"

namespace net::web_server {

namespace {
constexpr const char* kTag = "web_server";

// Single-page status/control UI: an 8-row table (relay state + DI
// interlock status + a toggle button per channel), polling
// GET /api/state every second and POSTing to /api/relay on a click.
// Deliberately no build step / separate JS file / framework -- this is
// the entire UI, kept small enough that inlining it is simpler than
// managing static assets on a microcontroller.
constexpr const char kIndexHtml[] = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AgIsoRelayBlock</title>
<style>
body{font-family:sans-serif;background:#f4f4f4;margin:0;padding:16px}
h1{font-size:1.3em;margin:0 0 4px}
#version{color:#888;font-size:0.8em;margin-bottom:16px}
table{border-collapse:collapse;width:100%;max-width:480px}
td,th{padding:8px;text-align:left;border-bottom:1px solid #ddd}
button{padding:8px 20px;font-size:1em;border:none;border-radius:4px;cursor:pointer;color:#fff}
button.on{background:#2e7d32}
button.off{background:#888}
button:disabled{background:#ccc;cursor:not-allowed;color:#666}
.di-active{color:#c62828;font-weight:bold}
a.ota-link{display:inline-block;margin-top:16px;color:#1565c0}
</style></head><body>
<h1>AgIsoRelayBlock</h1>
<div id="version">loading...</div>
<table><thead><tr><th>Channel</th><th>DI Interlock</th><th>Output</th></tr></thead>
<tbody id="rows"></tbody></table>
<a class="ota-link" href="/ota">Firmware update (OTA)</a>
<script>
function render(s) {
  document.getElementById('version').textContent = 'AgIsoRelayBlock ' + s.version;
  var rows = document.getElementById('rows');
  rows.innerHTML = '';
  for (var i = 0; i < 8; i++) {
    var ch = i + 1;
    var on = s.relays[i] != 0;
    var disabled = s.disabled[i] != 0;
    var tr = document.createElement('tr');
    tr.innerHTML = '<td>R' + ch + '</td>' +
      '<td class="' + (disabled ? 'di-active' : '') + '">' + (disabled ? 'ACTIVE' : '-') + '</td>' +
      '<td><button class="' + (on ? 'on' : 'off') + '"' + (disabled && !on ? ' disabled' : '') + '>' +
      (on ? 'ON' : 'OFF') + '</button></td>';
    tr.querySelector('button').onclick = (function(ch, on) {
      return function() { setRelay(ch, on ? 0 : 1); };
    })(ch, on);
    rows.appendChild(tr);
  }
}
function refresh() {
  fetch('/api/state').then(function(r) { return r.json(); }).then(render).catch(function() {});
}
function setRelay(ch, state) {
  fetch('/api/relay?ch=' + ch + '&state=' + state, { method: 'POST' }).then(refresh);
}
refresh();
setInterval(refresh, 1000);
</script>
</body></html>
)HTML";

constexpr const char kOtaHtml[] = R"HTML(<!DOCTYPE html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>AgIsoRelayBlock OTA</title>
<style>body{font-family:sans-serif;background:#f4f4f4;padding:16px}
#bar{width:100%;max-width:480px;background:#ddd;border-radius:4px;overflow:hidden;margin-top:12px;display:none}
#fill{height:20px;width:0;background:#2e7d32}
#status{margin-top:12px}
</style></head><body>
<h1>Firmware update</h1>
<p>Select an AgIsoRelayBlock <code>.bin</code> image built for this board.
The device reboots into the new image automatically once the upload
finishes; it only replaces the previous image if the new one boots and
claims its ISOBUS address successfully (otherwise it reverts on its own
next boot).</p>
<input type="file" id="file" accept=".bin">
<button onclick="upload()">Upload &amp; flash</button>
<div id="bar"><div id="fill"></div></div>
<div id="status"></div>
<script>
function upload() {
  var f = document.getElementById('file').files[0];
  if (!f) { return; }
  var bar = document.getElementById('bar');
  var fill = document.getElementById('fill');
  var status = document.getElementById('status');
  bar.style.display = 'block';
  var xhr = new XMLHttpRequest();
  xhr.open('POST', '/ota/upload');
  xhr.upload.onprogress = function(e) {
    if (e.lengthComputable) { fill.style.width = Math.round(100 * e.loaded / e.total) + '%'; }
  };
  xhr.onload = function() {
    status.textContent = xhr.status === 200 ? 'Flashed -- rebooting...' : ('Failed: ' + xhr.responseText);
  };
  xhr.onerror = function() { status.textContent = 'Upload failed (connection lost -- normal if the device already rebooted).'; };
  xhr.send(f);
}
</script>
</body></html>
)HTML";

esp_err_t index_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kIndexHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t ota_page_get_handler(httpd_req_t* req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, kOtaHtml, HTTPD_RESP_USE_STRLEN);
}

esp_err_t state_get_handler(httpd_req_t* req) {
    char body[256];
    int len = snprintf(body, sizeof(body), "{\"version\":\"%s\",\"relays\":[", esp_app_get_description()->version);
    for (int ch = 1; ch <= 8; ++ch) {
        len += snprintf(body + len, sizeof(body) - len, "%s%d", ch == 1 ? "" : ",", io::relay_driver::get_relay(ch) ? 1 : 0);
    }
    len += snprintf(body + len, sizeof(body) - len, "],\"disabled\":[");
    for (int ch = 1; ch <= 8; ++ch) {
        len += snprintf(body + len, sizeof(body) - len, "%s%d", ch == 1 ? "" : ",", automation::interlock::is_disabled(ch) ? 1 : 0);
    }
    len += snprintf(body + len, sizeof(body) - len, "]}");

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, len);
}

// Query params are the request's desired new state, not a toggle command
// -- the client always knows the last state it rendered (from /api/state)
// and sends the opposite explicitly, so a stale/duplicate click can't
// flip a relay twice.
esp_err_t relay_post_handler(httpd_req_t* req) {
    char query[64];
    int channel = 0;
    int state = -1;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char value[8];
        if (httpd_query_key_value(query, "ch", value, sizeof(value)) == ESP_OK) {
            channel = atoi(value);
        }
        if (httpd_query_key_value(query, "state", value, sizeof(value)) == ESP_OK) {
            state = atoi(value);
        }
    }

    if (channel < 1 || channel > 8 || (state != 0 && state != 1)) {
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "bad channel/state", HTTPD_RESP_USE_STRLEN);
    }

    // Same single source of truth as every other control path (SKM,
    // AUX-N) -- respects the DI interlock exactly like a toggle press
    // would (see iso::vt_app::set_relay_remote's doc comment). A refusal
    // isn't a server error, it's the interlock working as designed, so
    // this still returns 200 either way -- the next /api/state poll
    // shows the operator the real (unchanged) state regardless.
    bool ok = iso::vt_app::set_relay_remote(channel, state != 0);
    ESP_LOGI(kTag, "web: relay %d -> %s (%s)", channel, state ? "ON" : "OFF", ok ? "applied" : "refused (DI interlock)");
    return httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
}

// Streams the raw request body directly into the inactive OTA partition
// -- no multipart parsing, the upload page POSTs the .bin file's bytes
// as-is. Flashes then reboots on success; on any failure, aborts the OTA
// write and leaves the currently-running image untouched (esp_ota_abort,
// no partial/half-written image left bootable).
esp_err_t ota_upload_post_handler(httpd_req_t* req) {
    const esp_partition_t* target = esp_ota_get_next_update_partition(nullptr);
    if (!target) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "no OTA partition available", HTTPD_RESP_USE_STRLEN);
    }

    esp_ota_handle_t ota_handle;
    esp_err_t err = esp_ota_begin(target, OTA_SIZE_UNKNOWN, &ota_handle);
    if (ESP_OK != err) {
        ESP_LOGE(kTag, "esp_ota_begin failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "esp_ota_begin failed", HTTPD_RESP_USE_STRLEN);
    }

    char buffer[4096];
    int remaining = req->content_len;
    int received_total = 0;
    while (remaining > 0) {
        int received = httpd_req_recv(req, buffer, remaining < static_cast<int>(sizeof(buffer)) ? remaining : sizeof(buffer));
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  // just a slow client, keep waiting for the rest
            }
            ESP_LOGE(kTag, "OTA upload: connection error after %d bytes", received_total);
            esp_ota_abort(ota_handle);
            httpd_resp_set_status(req, "400 Bad Request");
            return httpd_resp_send(req, "upload interrupted", HTTPD_RESP_USE_STRLEN);
        }

        err = esp_ota_write(ota_handle, buffer, received);
        if (ESP_OK != err) {
            ESP_LOGE(kTag, "esp_ota_write failed: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            httpd_resp_set_status(req, "500 Internal Server Error");
            return httpd_resp_send(req, "esp_ota_write failed", HTTPD_RESP_USE_STRLEN);
        }
        remaining -= received;
        received_total += received;
    }

    err = esp_ota_end(ota_handle);
    if (ESP_OK != err) {
        // esp_ota_end() itself validates the image (magic byte, checksum);
        // this is where a corrupt/wrong-chip upload gets caught, before
        // ever touching the boot partition selection below.
        ESP_LOGE(kTag, "esp_ota_end failed (invalid image?): %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "400 Bad Request");
        return httpd_resp_send(req, "esp_ota_end failed -- invalid image", HTTPD_RESP_USE_STRLEN);
    }

    err = esp_ota_set_boot_partition(target);
    if (ESP_OK != err) {
        ESP_LOGE(kTag, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        return httpd_resp_send(req, "esp_ota_set_boot_partition failed", HTTPD_RESP_USE_STRLEN);
    }

    ESP_LOGI(kTag, "OTA upload complete (%d bytes), rebooting into new image", received_total);
    httpd_resp_send(req, "OK", HTTPD_RESP_USE_STRLEN);
    // The new image boots in "pending verify" state (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE,
    // sdkconfig.defaults) -- app_main.cpp only confirms it valid after it
    // proves itself by claiming its ISOBUS address; otherwise the
    // bootloader rolls back to this (still-intact) partition on its own.
    vTaskDelay(pdMS_TO_TICKS(500));  // let the HTTP response actually reach the browser first
    esp_restart();
    return ESP_OK;  // unreachable
}
}  // namespace

void init() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.uri_match_fn = httpd_uri_match_wildcard;
    // Firmware images are a few hundred KB; each recv() chunk above is
    // 4 KB, but the underlying multipart-free raw-body handler still
    // needs a generous stack for the TLS-free plain HTTP path here.
    config.stack_size = 8192;

    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &config) != ESP_OK) {
        ESP_LOGE(kTag, "httpd_start failed, web UI unavailable");
        return;
    }

    const httpd_uri_t routes[] = {
        {"/", HTTP_GET, index_get_handler, nullptr},
        {"/api/state", HTTP_GET, state_get_handler, nullptr},
        {"/api/relay", HTTP_POST, relay_post_handler, nullptr},
        {"/ota", HTTP_GET, ota_page_get_handler, nullptr},
        {"/ota/upload", HTTP_POST, ota_upload_post_handler, nullptr},
    };
    for (const auto& route : routes) {
        httpd_register_uri_handler(server, &route);
    }

    ESP_LOGI(kTag, "web server started");
}

}  // namespace net::web_server
