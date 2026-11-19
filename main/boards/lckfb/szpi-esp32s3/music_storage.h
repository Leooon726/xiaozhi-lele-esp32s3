#pragma once
#include "music_browser.h"

// Bounded RAM snapshots only; no filesystem I/O or scan is started by browsing.
TfMusic::BrowserPage GetLichuangMusicPage(size_t offset);
bool RequestLichuangMusicPath(const std::string& path);

// Transfer gate is protected by the music library mutex. The scanner completes
// and closes its files before unmount; no filesystem ownership is shared.
void BeginLichuangStorageTransfer();
void CancelLichuangStorageTransfer();
bool IsLichuangStorageQuiet();
bool UnmountLichuangMusicCard();
void InitializeLichuangMusicTools(bool refresh_immediately = false);
