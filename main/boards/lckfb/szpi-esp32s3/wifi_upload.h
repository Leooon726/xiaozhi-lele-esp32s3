#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <esp_http_server.h>
#include <atomic>
#include <string>

class LeleWifiUpload {
public:
    bool Start();
    void RequestStop();  // Nonblocking; main task retries if worker allocation fails.
    bool Stopped() const { return state_ == State::Stopped; }
    bool NeedsStop() const;
    const std::string& Address() const { return address_; }
    const char* Code() const { return code_; }
    unsigned Saved() const { return saved_.load(); }

private:
    enum class State { Stopped, Running, Stopping };
    std::atomic<State> state_{State::Stopped};
    std::atomic<bool> cancelled_{false};
    std::atomic<unsigned> saved_{0};
    httpd_handle_t server_ = nullptr;
    int64_t expires_at_ = 0;
    std::string address_;
    char code_[9]{};
    unsigned failures_ = 0;  // HTTP server task only.
    bool Authorized(httpd_req_t* request);
    bool Confirmed(httpd_req_t* request) const;
    bool Fresh(httpd_req_t* request) const;
    static esp_err_t Upload(httpd_req_t* request);
    static esp_err_t Files(httpd_req_t* request);
    static esp_err_t Stat(httpd_req_t* request);
    static esp_err_t Manage(httpd_req_t* request);
};
#endif
