#!/usr/bin/env bash
# Build and flash once. Requires ESP-IDF 4.4.x env.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$ROOT"

# Always use ESP-IDF v4.4.8 for this firmware, even if the shell already has another IDF loaded.
# Override with: IDF_EXPORT=/path/to/esp-idf-v4.4.8/export.sh ./flash.sh
if [[ -z "${IDF_EXPORT:-}" ]]; then
    if [[ -f "/mnt/data/Projects/esp/esp-idf-v4.4.8/export.sh" ]]; then
        IDF_EXPORT="/mnt/data/Projects/esp/esp-idf-v4.4.8/export.sh"
    else
        IDF_EXPORT="$HOME/Projects/esp/esp-idf-v4.4.8/export.sh"
    fi
fi

if [[ -f "$IDF_EXPORT" ]]; then
    unset IDF_PATH
    # shellcheck source=/dev/null
    . "$IDF_EXPORT"
else
    echo "error: esp-idf-v4.4.8 export.sh not found: $IDF_EXPORT" >&2
    echo "  Set IDF_EXPORT=/path/to/esp-idf-v4.4.8/export.sh" >&2
    exit 1
fi

PORT="${ESPPORT:-/dev/ttyUSB0}"
BAUD="${ESPBAUD:-921600}"

# idf.py fullclean
idf.py build
idf.py -p "$PORT" -b "$BAUD" flash
idf.py -p "$PORT" monitor