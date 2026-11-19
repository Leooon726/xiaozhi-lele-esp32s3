# AGENTS.md

## Lele project handoff (read first)

This repository extends XiaoZhi for the LiChuang Shizhanpai ESP32-S3.
Before changes, read `docs/lele/HANDOFF.md`, `BUILD_FLASH.md`, `DEBUGGING.md`,
and `VALIDATION.md`. They replace any reliance on prior chat history.
Current hardware baseline: `lichuang-dev-lele-music-debug`, wake phrase 乐乐,
SD-wide MP3 indexing with a dual-slot SD cache, and a local USB diagnostic entry.

- The user authorizes reversible local development; obtain current explicit
  authorization before any flash write/erase or OTA deployment. Build success
  is not permission to flash. Back up all 16 MiB and verify before writing.
- Never commit Flash/NVS backups, credentials, device identifiers, raw serial
  logs, local toolchains or generated artifacts. Keep them under `.local/` or
  `.tools/`, both ignored. Never force-add them, even to a private repository.
- Use `scripts/lele/build.sh` or canonical `scripts/build.py`; `env.sh` is
  project-local and must not modify the user's shell/system Python.
- Use `scripts/lele/music_debug.py` for diagnosis without asking the user to
  repeatedly wake the assistant. Serial opens may reset native USB devices.
- Preserve PSRAM MP3 task stacks and matching WithCaps deletion; do not restore
  the failing 32 KiB internal SRAM stack. Do not remove USB secondary-console
  registration/RX initialization from the debug entry.
- Current firmware (menu/MSC interaction not yet hardware-accepted): CONFIG_LICHUANG_USB_STORAGE replaces BOOT long-press
  playback with a 2-second menu and reboot-isolated MSC mode. Read
  docs/lele/USB_TRANSFER.md before touching USB, ownership or persistent mode.
  Keep transfer boot before camera/music/audio initialization. Consume the one-shot
  request before exporting SD; the next reset returns to assistant with a rescan.
  Never resume normal I/O in the same boot after partial unmount/close failures.
  The menu also has Display settings (light/dark and 5–100% brightness), flashed; basic switching and
  reboot persistence accepted by the user. Read docs/lele/DISPLAY.md. Pass display/backlight explicitly during
  board construction; never recursively call Board::GetInstance from that path.
  Assistant-area tapping in flashed 457e126 failed all three user tests;
  the source defers region creation until SetupUI completes (fix not flashed);
  read docs/lele/TOUCH.md.
  Keep gesture callbacks audio-free and dispatch only on the normal main loop.
- Do not format the SD card, assume a music/ directory, or restore stock automatic
  OTA in music variants. Keep debug controls behind their separate Kconfig flag.
- Update HANDOFF/VALIDATION when behavior or physical evidence changes. Distinguish
  direct playback success from the earlier cloud-selected file failure whose
  precise cause has not yet been demonstrated. Do not invent acceptance results.


## Project

XiaoZhi is an ESP-IDF C/C++ voice-assistant firmware supporting many chips, boards, displays, audio devices, and network transports. A build selects exactly one board implementation.

Use the ESP-IDF `release/v6.1` branch when possible. The minimum supported SDK is ESP-IDF v6.0.1. IDF 5.x is not supported.

## Architecture

- `main/application.*`: main event loop, protocol lifecycle, and high-level behavior.
- `main/device_state_machine.*`: legal runtime state transitions.
- `main/boards/common/`: board interfaces and reusable hardware/network helpers.
- `main/boards/**/`: board-specific pins, initialization, and build variants.
- `main/audio/`: codecs, audio tasks, engines, wake words, and queues.
- `main/protocols/`: transport-neutral API plus WebSocket and MQTT/UDP.
- `main/display/` and `main/led/`: reusable UI implementations.
- `main/mcp_server.*`: common device-side MCP tools and dispatch.
- `main/Kconfig.projbuild`: board and feature configuration.
- `main/CMakeLists.txt`: source, board, locale, font, and asset selection.
- `scripts/build.py`: canonical board/variant build entry point.

Read the closest existing implementation before adding a new one. Prefer the narrowest owning layer; do not put board-specific behavior into core modules.

## Required Rules

- Preserve unrelated worktree changes and keep patches focused.
- A build must export exactly one board factory through `DECLARE_BOARD(...)`.
- Never alter an existing board's pins to support different hardware. Add a uniquely named board or release variant; board identity affects OTA compatibility.
- Core code depends on `Board` interfaces, never a concrete board class or board `config.h`.
- Treat camera, backlight, display, LED, battery, and similar capabilities as optional.
- Change runtime state through `Application::SetDeviceState()` and the state machine.
- Callbacks may run outside the main task. Schedule application mutations with `Application::Schedule()` or event bits.
- Do not block the main event loop or audio tasks. Avoid unbounded queues and repeated large allocations in audio paths.
- Keep shared message semantics in `Protocol`; verify both transports when changing its contract.
- Validate network input and preserve `cJSON` ownership. NVS keys are persistent API and require migration when changed.
- Guard target-specific features with Kconfig/component rules. Do not assume every target has PSRAM or S3/P4 resources.
- Do not manually edit generated/vendor output: `build/`, `releases/`, `managed_components/`, `components/`, `sdkconfig*`, `main/assets/lang_config.h`, or generated mmap headers.
- Format only touched C/C++ files with the repository `.clang-format`; avoid unrelated mass formatting.

## Boards and Configuration

Board selection is a coupled chain:

`config.json` -> `scripts/build.py` -> `main/Kconfig.projbuild` -> `main/CMakeLists.txt` -> board source and `config.h`.

When adding a board or variant, update every relevant link in that chain. Include a unique board identity, correct chip target, flash/partition settings, exactly one `DECLARE_BOARD`, and board documentation. Follow `docs/custom-board.md`.

## Commands

Source the intended ESP-IDF environment first:

```sh
source /path/to/esp-idf/export.sh
idf.py --version
```

```sh
# Discover exact board and variant names
python3 scripts/build.py --list-boards

# Canonical variant build
python3 scripts/build.py <board-directory> --name <variant-name>

# Host-side build tests
python3 -m unittest discover -s scripts/tests -v

# Format/check touched files
clang-format -i <files>
clang-format --dry-run -Werror <files>
```

The build script changes local `sdkconfig` and build state. Do not assume the build directory still represents a previous target.

## Validation

- Board-only change: build affected variants and smoke-test changed hardware.
- Core, common-board, audio, protocol, display, dependency, Kconfig, or CMake change: run host tests and build representative affected chip/network paths.
- Protocol changes: verify WebSocket and MQTT/UDP when shared behavior changes.
- Audio changes: verify capture, playback, wake/VAD, interruption, reconnect, and applicable AEC modes.
- UI/assets changes: verify applicable no-display/OLED/LVGL paths and partition size.
- Always report what was tested and what still needs physical hardware. A successful build is not hardware validation.

## Authoritative Documentation

- Overview and SDK policy: `README.md`
- Board guide: `docs/custom-board.md`
- Audio design: `main/audio/README.md`
- Code style: `docs/code_style.md`
- Protocols: `docs/websocket.md`, `docs/mqtt-udp.md`, `docs/mcp-protocol.md`
- CI matrix: `.github/workflows/build.yml`

Keep detailed or fast-changing information in those files, not here. Add a nested `AGENTS.md` only when a subsystem needs specialized instructions.
