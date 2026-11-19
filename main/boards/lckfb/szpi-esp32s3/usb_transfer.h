#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <esp_err.h>

namespace LeleUsb {
// Value 1 was the old persistent transfer mode. Never reuse it as a request:
// an upgrade must recover an already stuck device into the assistant.
enum class BootMode { Assistant = 0, Refresh = 2, Transfer = 3 };
BootMode ReadBootMode();
esp_err_t SaveBootMode(BootMode mode);
// Consume the request before exporting SD; a later reset returns to assistant.
bool TakeTransferBootRequest();
esp_err_t Start();
bool HostReleased();
bool Failed();
// Requires owner handoff (or no host), stops USB and closes storage before reboot.
esp_err_t Finish();
}  // namespace LeleUsb
#endif
