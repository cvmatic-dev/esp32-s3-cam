# shellcheck shell=bash disable=SC2317
# (SC2317: the "return || exit" idiom works whether sourced or executed)
# Source me:  . scripts/env.sh
# Activates the pinned ESP-IDF. Idempotent and quiet. Safe for agents.

ESP_IDF_VERSION_PIN="${ESP_IDF_VERSION_PIN:-v5.5.2}"
export ESP_IDF_VERSION_PIN
: "${IDF_PATH:=$HOME/esp/esp-idf-${ESP_IDF_VERSION_PIN}}"
export IDF_PATH

if ! command -v idf.py >/dev/null 2>&1 || [ "${_ESP32CAM_ENV:-}" != "$IDF_PATH" ]; then
    if [ ! -f "$IDF_PATH/export.sh" ]; then
        echo "env.sh: ESP-IDF not found at $IDF_PATH — run scripts/setup_host.sh first" >&2
        return 3 2>/dev/null || exit 3
    fi
    # shellcheck disable=SC1091
    . "$IDF_PATH/export.sh" >/dev/null 2>&1 || {
        echo "env.sh: sourcing $IDF_PATH/export.sh failed (run: $IDF_PATH/install.sh esp32s3)" >&2
        return 3 2>/dev/null || exit 3
    }
    export _ESP32CAM_ENV="$IDF_PATH"
fi

# Plain logs for machines.
export IDF_CCACHE_ENABLE=1
export PYTHONUNBUFFERED=1
