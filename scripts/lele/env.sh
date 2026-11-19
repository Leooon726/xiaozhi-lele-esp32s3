#!/usr/bin/env bash
# Source in the current shell; this never edits ~/.zshrc or system Python.
LELE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
export IDF_PATH="${LELE_IDF_PATH:-$LELE_ROOT/.tools/idf-sdk/esp-idf-v6.1.1}"
export IDF_TOOLS_PATH="${LELE_IDF_TOOLS_PATH:-$LELE_ROOT/.tools/espressif}"
export PIP_CACHE_DIR="$LELE_ROOT/.local/pip-cache"
export XDG_CACHE_HOME="$LELE_ROOT/.local/cache"
export IDF_COMPONENT_CACHE_PATH="$LELE_ROOT/.local/component-cache"
export PATH="$LELE_ROOT/.tools/build-host/bin:$LELE_ROOT/.tools/python/cpython-3.12.16-macos-aarch64-none/bin:$PATH"
if [[ ! -f "$IDF_PATH/export.sh" ]]; then
    echo 'ESP-IDF missing: see docs/lele/BUILD_FLASH.md; set LELE_IDF_PATH and LELE_IDF_TOOLS_PATH.' >&2
    return 1
fi
source "$IDF_PATH/export.sh"
