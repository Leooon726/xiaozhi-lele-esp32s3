#pragma once

// Transfer gate is protected by the music library mutex. The scanner completes
// and closes its files before unmount; no filesystem ownership is shared.
void BeginLichuangStorageTransfer();
void CancelLichuangStorageTransfer();
bool IsLichuangStorageQuiet();
bool UnmountLichuangMusicCard();
void InitializeLichuangMusicTools(bool refresh_immediately = false);
