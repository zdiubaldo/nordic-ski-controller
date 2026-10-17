#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.."
export ANDROID_HOME="$PWD/.local/android-tools/sdk"
export ANDROID_USER_HOME="$PWD/.local/android-user"
export ANDROID_AVD_HOME="$PWD/.local/avd"
export JAVA_HOME="${JAVA_HOME:-$(find "$PWD/.local/android-tools/java" -mindepth 1 -maxdepth 1 -type d -print -quit)}"
mkdir -p "$ANDROID_USER_HOME" "$ANDROID_AVD_HOME"
if [[ ! -f "$ANDROID_AVD_HOME/NordicSkiAPI36.ini" ]]; then
  "$ANDROID_HOME/cmdline-tools/latest/bin/avdmanager" create avd --name NordicSkiAPI36 \
    --package 'system-images;android-36;google_apis;x86_64' --device pixel_4 \
    --path "$ANDROID_AVD_HOME/NordicSkiAPI36.avd" <<<'no'
fi
exec "$ANDROID_HOME/emulator/emulator" -avd NordicSkiAPI36 -port 5554 \
  -memory 2048 -cores 2 -gpu "${NORDIC_EMULATOR_GPU:-host}" -feature -Vulkan -no-audio -no-snapshot \
  -packet-streamer-endpoint default -no-boot-anim "$@"
