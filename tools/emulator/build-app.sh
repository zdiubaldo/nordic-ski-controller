#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.."
export NORDIC_ANDROID_ARCHITECTURES=x86_64
export NORDIC_ANDROID_APK_NAME=nordic-ski-emulator.apk
exec bash tools/build-android-local.sh
