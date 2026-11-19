#!/usr/bin/env bash
set -euo pipefail
LELE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
source "$LELE_ROOT/scripts/lele/env.sh"
cd "$LELE_ROOT"
LELE_VARIANT="${1:-lichuang-dev-lele-music-debug}"
case "$LELE_VARIANT" in
    lichuang-dev-lele|lichuang-dev-lele-music|lichuang-dev-lele-music-debug) ;;
    *) echo 'Use a documented Lele variant.' >&2; exit 2 ;;
esac
python -u scripts/build.py lckfb/szpi-esp32s3 --name "$LELE_VARIANT" --language en-US --wake-word 'custom:le le:乐乐' --zip
