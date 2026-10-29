# Single entry point for humans and agents. Every target is non-interactive
# except `menuconfig` and `monitor-interactive`.
SHELL := /bin/bash
ENV   := . scripts/env.sh &&

.PHONY: help build flash test capture observe monitor-interactive ports reconfigure \
        menuconfig size clean fullclean format debug-gdb

help:            ## list targets
	@grep -E '^[a-z-]+:.*##' $(MAKEFILE_LIST) | sed 's/:.*##/\t/'

build:           ## compile (errors summarised at the end)
	@scripts/build.sh

flash: build     ## build + flash (holds port lock)
	@scripts/flash.sh

test:            ## build + flash + reset + run self-test -> exit code, build/hw/*
	@scripts/test_hw.sh

capture:         ## build + flash + run the capture/stream utility (720p grey -> SD + WiFi MJPEG)
	@rm -f sdkconfig
	@SDKCONFIG_DEFAULTS="sdkconfig.defaults;scripts/sdkconfig.capture" scripts/test_hw.sh

observe:         ## reset and watch the running firmware (no build/flash)
	@NO_FLASH=1 scripts/test_hw.sh

ports:           ## show detected ESP32 serial ports
	@python3 tools/esp_monitor.py --list

reconfigure:     ## regenerate sdkconfig from sdkconfig.defaults
	@$(ENV) rm -f sdkconfig && idf.py reconfigure >/dev/null && echo "sdkconfig regenerated"

size:            ## per-component size report
	@$(ENV) idf.py size-components

clean:           ## remove app build outputs
	@$(ENV) idf.py clean

fullclean:       ## wipe build dir entirely (needed after target/IDF changes)
	@$(ENV) idf.py fullclean

format:          ## clang-format the app sources
	@clang-format -i main/*.c main/*.h

# ---- human-only (interactive) -------------------------------------------------
menuconfig:      ## [human] interactive config; then copy changes into sdkconfig.defaults
	@$(ENV) idf.py menuconfig && idf.py save-defconfig && echo "-> review diff of sdkconfig.defaults"

monitor-interactive: ## [human] classic idf.py monitor (Ctrl+] to exit)
	@$(ENV) idf.py -p $$(python3 tools/esp_monitor.py --resolve-port | head -n1) monitor

debug-gdb:       ## [human] OpenOCD + GDB over built-in USB-JTAG
	@$(ENV) idf.py openocd gdb
