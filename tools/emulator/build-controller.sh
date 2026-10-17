#!/usr/bin/env bash
set -euo pipefail
cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.."
mkdir -p .local
if ! command -v cc >/dev/null 2>&1; then
  exec podman run --rm --network=none --security-opt label=disable \
    -v "$PWD:/project" -w /project docker.io/espressif/idf:v6.1 bash tools/emulator/build-controller.sh
fi
cc -std=c11 -Wall -Wextra -Werror \
  -Ifirmware/components/control/include -Ifirmware/components/test_session/include \
  -Ifirmware/components/motion/include -Ifirmware/components/control_service/include \
  firmware/components/control/control.c firmware/components/test_session/test_session.c \
  firmware/components/motion/motion.c firmware/components/motion/motion_memory.c \
  firmware/components/control_service/control_service.c preview/controller.c -lm -o .local/ble-sim-controller
