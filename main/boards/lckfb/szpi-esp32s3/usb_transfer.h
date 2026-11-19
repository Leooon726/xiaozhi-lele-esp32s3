#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_LICHUANG_USB_STORAGE
#include <esp_err.h>

namespace LeleUsb {
enum class BootMode { Assistant = 0, Transfer = 1, Refresh = 2 };
BootMode ReadBootMode();
esp_err_t SaveBootMode(BootMode mode);
esp_err_t Start();
bool HostReleased();
bool Failed();
// Requires host eject, stops USB and closes the storage before allowing reboot.
esp_err_t Finish();
}  // namespace LeleUsb
#endif
