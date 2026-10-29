#!/usr/bin/env bash
# One-time setup of the dev host (dellws, Ubuntu/Debian). Run by the HUMAN, not
# by an agent — it uses sudo. Idempotent; safe to re-run.
#
#   scripts/setup_host.sh            # everything
#   scripts/setup_host.sh --no-sudo  # only ESP-IDF in $HOME (skip apt/udev/groups)
set -euo pipefail
cd "$(dirname "$0")/.."

ESP_IDF_VERSION_PIN="${ESP_IDF_VERSION_PIN:-v6.0.3}"
IDF_PATH="${IDF_PATH:-$HOME/esp/esp-idf-${ESP_IDF_VERSION_PIN}}"
NO_SUDO=0; [ "${1:-}" = "--no-sudo" ] && NO_SUDO=1

if [ "$NO_SUDO" = 0 ]; then
    echo "== apt packages"
    sudo apt-get update -qq
    sudo apt-get install -y -qq git wget flex bison gperf python3 python3-pip python3-venv \
        cmake ninja-build ccache libffi-dev libssl-dev dfu-util libusb-1.0-0 util-linux

    echo "== udev rules + groups"
    sudo install -m 0644 tools/99-esp32cam.rules /etc/udev/rules.d/99-esp32cam.rules
    sudo udevadm control --reload-rules && sudo udevadm trigger
    sudo usermod -aG dialout,plugdev "$USER"
    echo "   (log out/in once for group changes to apply)"
fi

echo "== ESP-IDF $ESP_IDF_VERSION_PIN -> $IDF_PATH"
if [ ! -d "$IDF_PATH/.git" ]; then
    mkdir -p "$(dirname "$IDF_PATH")"
    git clone -b "$ESP_IDF_VERSION_PIN" --recursive --shallow-submodules --depth 1 \
        https://github.com/espressif/esp-idf.git "$IDF_PATH"
fi
"$IDF_PATH/install.sh" esp32s3

echo "== smoke test"
# shellcheck source=scripts/env.sh
. scripts/env.sh
idf.py --version
python3 tools/esp_monitor.py --list || true

echo
echo "Done. Plug the board's native USB port into dellws, then: make test"
