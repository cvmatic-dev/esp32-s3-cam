cmake_minimum_required(VERSION 3.16)

# Pinned toolchain/framework: see scripts/env.sh (IDF_VERSION) and AGENTS.md.
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Firmware version string baked into the @@BOOT marker.
set(PROJECT_VER "0.1.0")

project(esp32s3_cam)
