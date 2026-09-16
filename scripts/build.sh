#!/usr/bin/env bash
# 构建 badge 固件（ESP32-S3-Touch-AMOLED-1.75C）
# FQBN 取自官方仓库 CI（scripts/discover_examples.py）
set -euo pipefail
PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FQBN='esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,FlashMode=qio,PartitionScheme=app3M_fat9M_16MB,USBMode=hwcdc,CDCOnBoot=cdc'
export UV_CACHE_DIR="${UV_CACHE_DIR:-$PROJECT_DIR/build/uv-cache}"
uv run --no-project "$PROJECT_DIR/scripts/generate_wifi_config.py" \
  "$PROJECT_DIR/firmware/badge/wifi_defaults.h"
exec arduino-cli compile --fqbn "$FQBN" \
  --libraries "$PROJECT_DIR/libraries" \
  --build-path "$PROJECT_DIR/build" \
  "$PROJECT_DIR/firmware/badge"
