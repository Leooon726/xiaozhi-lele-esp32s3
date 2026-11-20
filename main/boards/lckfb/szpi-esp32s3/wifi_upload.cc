#include "wifi_upload.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <net/if.h>
#include <psa/crypto.h>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <memory>
#include <new>
#include "sd_file_manager.h"
#include "wifi_file_page.h"
#include "wifi_manager.h"
#include "wifi_upload_file.h"

namespace {
constexpr int64_t kSessionUs = 10LL * 60 * 1000000;
constexpr int64_t kUploadUs = 2LL * 60 * 1000000;
constexpr int64_t kReceiveIdleUs = 20LL * 1000000;
constexpr char kSdRoot[] = "/sdcard";
constexpr auto& kPage = LeleUpload::kFilePage;

esp_err_t Error(httpd_req_t* request, const char* status, const char* message) {
    httpd_resp_set_status(request, status);
    httpd_resp_set_hdr(request, "Connection", "close");
    httpd_resp_set_type(request, "text/plain");
    httpd_resp_sendstr(request, message);
    return ESP_FAIL;  // Close: an unconsumed request body must not become another request.
}

bool ReadQuery(httpd_req_t* request, LeleUpload::Query& query) {
    const auto length = httpd_req_get_url_query_len(request);
    if (length > LeleUpload::kMaxQueryBytes)
        return false;
    auto data = std::unique_ptr<char[]>(new (std::nothrow) char[length + 1]{});
    return data &&
           (!length || httpd_req_get_url_query_str(request, data.get(), length + 1) == ESP_OK) &&
           query.Parse(std::string_view(data.get(), length));
}

std::string Quoted(const std::string& value) {
    std::string result = "\"";
    for (char c : value) {
        if (c == '"' || c == '\\')
            result += '\\';
        result += c;
    }
    return result + "\"";
}

esp_err_t Json(httpd_req_t* request, const std::string& body) {
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, body.data(), body.size());
}

esp_err_t FileError(httpd_req_t* request, LeleUpload::Result result) {
    using LeleUpload::Result;
    return Error(request,
                 result == Result::Invalid    ? "400 Bad Request"
                 : result == Result::NotFound ? "404 Not Found"
                 : (result == Result::Exists || result == Result::NotEmpty)
                     ? "409 Conflict"
                     : "500 Internal Server Error",
                 result == Result::Invalid    ? "Invalid or protected SD path."
                 : result == Result::NotFound ? "Path not found."
                 : result == Result::Exists   ? "Destination already exists."
                 : result == Result::NotEmpty ? "Folder is not empty; remove its contents first."
                 : result == Result::Recovery ? "Replacement failed; original retained in a hidden "
                                                ".lele-old-* recovery file. Inspect it with USB."
                                              : "SD operation failed.");
}
}  // namespace

bool LeleWifiUpload::NeedsStop() const {
    return cancelled_ || (state_ != State::Stopped && esp_timer_get_time() >= expires_at_);
}

bool LeleWifiUpload::Start() {
    if (!Stopped())
        return false;
    auto& wifi = WifiManager::GetInstance();
    const auto ip = wifi.GetIpAddress();
    if (!wifi.IsConnected() || ip.empty() || ip == "0.0.0.0")
        return false;
    auto* netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    struct ifreq interface = {};
    if (!netif || esp_netif_get_netif_impl_name(netif, interface.ifr_name) != ESP_OK)
        return false;
    std::snprintf(code_, sizeof(code_), "%06lu",
                  static_cast<unsigned long>(esp_random() % 1000000));
    address_ = "http://" + ip + ":8080";
    expires_at_ = esp_timer_get_time() + kSessionUs;
    cancelled_ = false;
    saved_ = 0;
    failures_ = 0;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 8080;
    config.ctrl_port = 32769;  // Separate from the Wi-Fi provisioning server.
    config.if_name = &interface;
    config.stack_size = 8192;
    config.task_priority = 1;
    config.max_open_sockets = 2;
    config.max_uri_handlers = 5;
    config.max_uri_len = 1536;
    config.recv_wait_timeout = 5;
    config.send_wait_timeout = 5;
    config.lru_purge_enable = true;
    if (httpd_start(&server_, &config) != ESP_OK) {
        server_ = nullptr;
        return false;
    }
    state_ = State::Running;
    httpd_uri_t page = {};
    page.uri = "/";
    page.method = HTTP_GET;
    page.user_ctx = this;
    page.handler = [](httpd_req_t* request) -> esp_err_t {
        auto* upload = static_cast<LeleWifiUpload*>(request->user_ctx);
        if (upload->NeedsStop())
            return Error(request, "503 Service Unavailable", "Session ended.");
        httpd_resp_set_type(request, "text/html; charset=utf-8");
        httpd_resp_set_hdr(request, "Cache-Control", "no-store");
        return httpd_resp_send(request, kPage, sizeof(kPage) - 1);
    };
    httpd_uri_t upload = {};
    upload.uri = "/upload";
    upload.method = HTTP_POST;
    upload.handler = Upload;
    upload.user_ctx = this;
    httpd_uri_t files = {};
    files.uri = "/files";
    files.method = HTTP_GET;
    files.handler = Files;
    files.user_ctx = this;
    httpd_uri_t info = {};
    info.uri = "/stat";
    info.method = HTTP_GET;
    info.handler = Stat;
    info.user_ctx = this;
    httpd_uri_t manage = {};
    manage.uri = "/manage";
    manage.method = HTTP_POST;
    manage.handler = Manage;
    manage.user_ctx = this;
    for (const auto* route : {&page, &upload, &files, &info, &manage}) {
        if (httpd_register_uri_handler(server_, route) != ESP_OK) {
            RequestStop();
            return false;
        }
    }
    return true;
}

void LeleWifiUpload::RequestStop() {
    cancelled_ = true;
    auto expected = State::Running;
    if (!state_.compare_exchange_strong(expected, State::Stopping))
        return;
    if (xTaskCreate(
            [](void* arg) {
                auto* upload = static_cast<LeleWifiUpload*>(arg);
                // Never wait for an active socket/SD write on the application task.
                if (httpd_stop(upload->server_) == ESP_OK) {
                    upload->server_ = nullptr;
                    upload->state_ = State::Stopped;
                } else {
                    upload->state_ = State::Running;  // Keep storage gated and retry.
                }
                vTaskDelete(nullptr);
            },
            "wifi_upload_stop", 4096, this, 1, nullptr) != pdPASS) {
        state_ = State::Running;
    }
}

bool LeleWifiUpload::Authorized(httpd_req_t* request) {
    char supplied[sizeof(code_)]{};
    unsigned different = 0;
    if (httpd_req_get_hdr_value_len(request, "X-Lele-Key") != sizeof(code_) - 1 ||
        httpd_req_get_hdr_value_str(request, "X-Lele-Key", supplied, sizeof(supplied)) != ESP_OK)
        different = 1;
    for (size_t i = 0; i < sizeof(code_) - 1; ++i)
        different |= static_cast<unsigned char>(supplied[i] ^ code_[i]);
    if (different && ++failures_ >= 8)
        cancelled_ = true;
    return !different && !NeedsStop();
}

bool LeleWifiUpload::Confirmed(httpd_req_t* request) const {
    char text[4]{};
    return httpd_req_get_hdr_value_len(request, "X-Lele-Confirm") == 3 &&
           httpd_req_get_hdr_value_str(request, "X-Lele-Confirm", text, sizeof(text)) == ESP_OK &&
           std::string_view(text) == "yes";
}

bool LeleWifiUpload::Fresh(httpd_req_t* request) const {
    char text[11]{};
    const auto length = httpd_req_get_hdr_value_len(request, "X-Lele-Revision");
    return length > 0 && length < sizeof(text) &&
           httpd_req_get_hdr_value_str(request, "X-Lele-Revision", text, sizeof(text)) == ESP_OK &&
           text == std::to_string(Saved());
}

esp_err_t LeleWifiUpload::Files(httpd_req_t* request) {
    auto* upload = static_cast<LeleWifiUpload*>(request->user_ctx);
    if (!upload->Authorized(request))
        return Error(request, "403 Forbidden", "Code incorrect or session ended.");
    LeleUpload::Query query;
    if (request->content_len || !ReadQuery(request, query) || !query.to.empty() ||
        !query.op.empty() || !query.name.empty() || query.replace)
        return Error(request, "400 Bad Request", "Invalid listing query.");
    if (query.offset && !upload->Fresh(request))
        return Error(request, "409 Conflict", "Directory changed; refresh from the first page.");
    LeleUpload::Page page;
    const auto result = LeleUpload::Manager(kSdRoot).List(query.path, query.offset, page);
    if (result != LeleUpload::Result::Ok)
        return FileError(request, result);
    std::string body = "{\"path\":" + Quoted(query.path) +
                       ",\"revision\":" + std::to_string(upload->Saved()) +
                       ",\"next\":" + (page.more ? std::to_string(page.next) : "null") +
                       ",\"limited\":" + (page.limited ? "true" : "false") + ",\"entries\":[";
    bool first = true;
    for (const auto& entry : page.entries) {
        if (!first)
            body += ',';
        first = false;
        body += "{\"name\":" + Quoted(entry.name) +
                ",\"directory\":" + (entry.directory ? "true" : "false") +
                ",\"bytes\":" + std::to_string(entry.bytes) + "}";
    }
    return Json(request, body + "]}");
}

esp_err_t LeleWifiUpload::Stat(httpd_req_t* request) {
    auto* upload = static_cast<LeleWifiUpload*>(request->user_ctx);
    if (!upload->Authorized(request))
        return Error(request, "403 Forbidden", "Code incorrect or session ended.");
    LeleUpload::Query query;
    if (request->content_len || !ReadQuery(request, query) || !query.has_path ||
        !query.to.empty() || !query.op.empty() || !query.name.empty() || query.replace ||
        query.offset)
        return Error(request, "400 Bad Request", "Invalid stat query.");
    LeleUpload::Entry entry;
    const auto result = LeleUpload::Manager(kSdRoot).Info(query.path, entry);
    if (result != LeleUpload::Result::Ok)
        return FileError(request, result);
    return Json(request, "{\"directory\":" + std::string(entry.directory ? "true" : "false") +
                             ",\"bytes\":" + std::to_string(entry.bytes) +
                             ",\"revision\":" + std::to_string(upload->Saved()) + "}");
}

esp_err_t LeleWifiUpload::Manage(httpd_req_t* request) {
    auto* upload = static_cast<LeleWifiUpload*>(request->user_ctx);
    if (!upload->Authorized(request))
        return Error(request, "403 Forbidden", "Code incorrect or session ended.");
    LeleUpload::Query query;
    if (request->content_len || !ReadQuery(request, query) || !query.has_path ||
        !query.name.empty() || query.replace || query.offset)
        return Error(request, "400 Bad Request", "Invalid management query.");
    LeleUpload::Manager manager(kSdRoot);
    LeleUpload::Result result;
    if (query.op == "mkdir" && query.to.empty()) {
        result = manager.Mkdir(query.path);
    } else if ((query.op == "delete" && query.to.empty()) || query.op == "move") {
        if (!upload->Confirmed(request) || !upload->Fresh(request))
            return Error(request, "409 Conflict",
                         "Confirm the operation against a fresh directory revision.");
        result =
            query.op == "delete" ? manager.Remove(query.path) : manager.Move(query.path, query.to);
    } else {
        return Error(request, "400 Bad Request", "Unknown operation.");
    }
    if (result != LeleUpload::Result::Ok)
        return FileError(request, result);
    ++upload->saved_;
    return Json(request, "{\"ok\":true,\"revision\":" + std::to_string(upload->Saved()) + "}");
}

esp_err_t LeleWifiUpload::Upload(httpd_req_t* request) {
    auto* upload = static_cast<LeleWifiUpload*>(request->user_ctx);
    if (!upload->Authorized(request))
        return Error(request, "403 Forbidden", "Code incorrect or session ended.");
    if (!request->content_len || request->content_len > LeleUpload::kMaxFileBytes)
        return Error(request, "413 Content Too Large", "File must be 1 byte to 64 MiB.");
    LeleUpload::Query query;
    if (!ReadQuery(request, query) || !query.to.empty() || !query.op.empty() || query.offset ||
        (query.has_path && !query.name.empty()))
        return Error(request, "400 Bad Request", "Invalid upload query.");
    if (query.replace && (!upload->Confirmed(request) || !upload->Fresh(request)))
        return Error(request, "409 Conflict",
                     "Confirm replacement against a fresh directory revision.");
    LeleUpload::Manager manager(kSdRoot);
    if (!query.has_path) {
        if (!LeleUpload::ValidName(query.name))
            return Error(request, "400 Bad Request", "Invalid filename.");
        const auto created = manager.Mkdir("uploads");
        if (created != LeleUpload::Result::Ok && created != LeleUpload::Result::Exists)
            return FileError(request, created);
        query.path = "uploads/" + query.name;  // Compatible with the first upload API.
    }
    if (query.path.empty())
        return Error(request, "400 Bad Request", "File path required.");
    std::string absolute;
    const auto resolved = manager.Resolve(query.path, absolute, true);
    if (resolved != LeleUpload::Result::Ok)
        return FileError(request, resolved);
    const auto slash = absolute.find_last_of('/');
    char nonce[17];
    std::snprintf(nonce, sizeof(nonce), "%08lx%08lx", static_cast<unsigned long>(esp_random()),
                  static_cast<unsigned long>(esp_random()));
    LeleUpload::File file;
    const auto opened = file.Open(absolute.substr(0, slash), absolute.substr(slash + 1),
                                  request->content_len, nonce, query.replace);
    if (opened == LeleUpload::Result::Exists)
        return Error(request, "409 Conflict",
                     "Filename already exists; rename it before uploading.");
    if (opened != LeleUpload::Result::Ok)
        return Error(request, "500 Internal Server Error", "Cannot create SD file.");
    auto buffer = std::unique_ptr<char[]>(new (std::nothrow) char[2048]);
    if (!buffer)
        return Error(request, "503 Service Unavailable", "Not enough memory.");
    psa_hash_operation_t hash = PSA_HASH_OPERATION_INIT;
    if (psa_crypto_init() != PSA_SUCCESS || psa_hash_setup(&hash, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        psa_hash_abort(&hash);
        return Error(request, "500 Internal Server Error", "Hash initialization failed.");
    }
    size_t remaining = request->content_len;
    unsigned chunks = 0;
    auto last_progress = esp_timer_get_time();
    const auto deadline = last_progress + kUploadUs;
    while (remaining && !upload->NeedsStop() && esp_timer_get_time() < deadline) {
        const int received =
            httpd_req_recv(request, buffer.get(), std::min(remaining, size_t(2048)));
        if (received == HTTPD_SOCK_ERR_TIMEOUT) {
            ESP_LOGW("LeleUpload", "Receive timeout: bytes=%u/%u",
                     unsigned(request->content_len - remaining), unsigned(request->content_len));
            if (esp_timer_get_time() - last_progress >= kReceiveIdleUs) {
                psa_hash_abort(&hash);
                return Error(request, "408 Request Timeout",
                             "Upload receive timed out (20 seconds without data); file not saved.");
            }
            continue;  // A socket wait timeout is recoverable; overall/idle limits remain bounded.
        }
        if (received <= 0) {
            ESP_LOGE("LeleUpload", "Receive failed: result=%d errno=%d bytes=%u/%u", received,
                     errno, unsigned(request->content_len - remaining),
                     unsigned(request->content_len));
            psa_hash_abort(&hash);
            return Error(request, "500 Internal Server Error",
                         "Upload connection closed or receive failed; file not saved.");
        }
        if (!file.Write(buffer.get(), received)) {
            ESP_LOGE("LeleUpload", "SD write failed: errno=%d bytes=%u/%u chunk=%d", errno,
                     unsigned(request->content_len - remaining), unsigned(request->content_len),
                     received);
            psa_hash_abort(&hash);
            return Error(request, "500 Internal Server Error", "SD write failed; file not saved.");
        }
        const auto hash_result =
            psa_hash_update(&hash, reinterpret_cast<const uint8_t*>(buffer.get()), received);
        if (hash_result != PSA_SUCCESS) {
            ESP_LOGE("LeleUpload", "Checksum update failed: status=%ld bytes=%u/%u",
                     static_cast<long>(hash_result), unsigned(request->content_len - remaining),
                     unsigned(request->content_len));
            psa_hash_abort(&hash);
            return Error(request, "500 Internal Server Error",
                         "Upload checksum update failed; file not saved.");
        }
        remaining -= received;
        last_progress = esp_timer_get_time();
        if (++chunks % 16 == 0)
            vTaskDelay(1);
    }
    if (remaining || upload->NeedsStop() || esp_timer_get_time() >= deadline) {
        psa_hash_abort(&hash);
        return Error(request, "408 Request Timeout",
                     "Transfer cancelled or timed out; file not saved.");
    }
    uint8_t digest[32];
    size_t length = 0;
    if (psa_hash_finish(&hash, digest, sizeof(digest), &length) != PSA_SUCCESS ||
        length != sizeof(digest)) {
        psa_hash_abort(&hash);
        return Error(request, "500 Internal Server Error",
                     "Hash finalization failed; file not saved.");
    }
    const auto saved = file.Finish();
    if (saved == LeleUpload::Result::Recovery) {
        upload->cancelled_ = true;
        return FileError(request, saved);
    }
    if (saved != LeleUpload::Result::Ok)
        return Error(
            request,
            saved == LeleUpload::Result::Exists ? "409 Conflict" : "500 Internal Server Error",
            "SD commit failed or filename exists; file not saved.");
    ++upload->saved_;
    char hex[65];
    for (unsigned i = 0; i < sizeof(digest); ++i)
        std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    char response[160];
    std::snprintf(response, sizeof(response),
                  "{\"saved\":true,\"bytes\":%u,\"sha256\":\"%s\",\"backup_kept\":%s}",
                  unsigned(request->content_len), hex, file.BackupRetained() ? "true" : "false");
    httpd_resp_set_type(request, "application/json");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, response);
}
#endif
