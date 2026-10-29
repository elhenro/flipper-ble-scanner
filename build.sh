#!/usr/bin/env bash
# build patched momentum (full ble stack + scan api) and ble_scanner fap
# usage: ./build.sh [fw|app|flash-fw|flash-dfu|flash-app]   (default: fw + app)
set -euo pipefail
cd "$(dirname "$0")"

ROOT=$PWD
FW=vendor/momentum
# must match device firmware, fap api version derives from it
COMMIT=d3f89dfe2ef6b01839201598e9be1590cba80322
# stock ob.data expects light stack secure layout (SFSA/SBRV/SBRSA/SNBRSA), updater
# flags full stack values as uncorrectable without this
STACK=(
  COPRO_STACK_TYPE=ble_full
  COPRO_STACK_BIN=stm32wb5x_BLE_Stack_full_fw.bin
  # fbt prefixes ROOT_DIR, so relative to vendor/momentum
  COPRO_OB_DATA=../../ob_full.data
)

if [ ! -d "$FW/.git" ]; then
  git init -q "$FW"
  git -C "$FW" remote add origin https://github.com/Next-Flip/Momentum-Firmware.git
  git -C "$FW" fetch -q --depth 1 origin "$COMMIT"
  git -C "$FW" checkout -q FETCH_HEAD
  git -C "$FW" submodule update -q --init --recursive --depth 1
fi

# idempotent: skip when already applied
git -C "$FW" apply --reverse --check "$ROOT/patches/momentum-ble-scan.patch" 2>/dev/null ||
  git -C "$FW" apply "$ROOT/patches/momentum-ble-scan.patch"
ln -sfn "$ROOT/app" "$FW/applications_user/ble_scanner"

cd "$FW"
fbt() { nice -n 10 ./fbt "${STACK[@]}" "$@"; }

# firmware only over stm32 rom dfu, no sd card involved. radio stack must already be full
# (updater on this unit fails to mount the old sd card often, see README)
flash_dfu() {
  local dfu
  dfu=$(ls dist/f7-C/f7-update-*/firmware.dfu | head -1)
  # fbt toolchain python ships pyserial, momentum's resolver finds the flipper port.
  # no port = already in dfu (or unplugged), the wait below covers both
  # cli drops input written right after the port opens, hence the pause
  (cd scripts && ../toolchain/current/bin/python3 -c '
import logging, serial, time
from flipper.utils.cdc import resolve_port
port = resolve_port(logging.getLogger(), "auto")
if port:
    with serial.Serial(port, timeout=1) as s:
        time.sleep(0.5)
        s.write(b"\rpower reboot2dfu\r")
        s.flush()
') || true
  local tries=0
  until dfu-util -l 2>/dev/null | grep -q 0483:df11; do
    if ((++tries > 30)); then
      echo "no dfu device: hold Left+Back until reboot, then release Back and keep Left" >&2
      return 1
    fi
    sleep 1
  done
  # leave request resets the chip mid-transfer, dfu-util then exits with a bogus
  # get_status error, so judge success by its output instead of the exit code
  local out
  out=$(dfu-util -d 0483:df11 -a 0 -D "$dfu" -s :leave 2>&1 || true)
  echo "$out"
  grep -q "Download done" <<<"$out"
}

case "${1:-all}" in
  fw) fbt updater_package ;;
  app) fbt fap_ble_scanner ;;
  # uploads package over usb serial, device reflashes firmware + radio stack
  flash-fw) fbt flash_usb_full ;;
  flash-dfu) flash_dfu ;;
  flash-app) fbt launch APPSRC=applications_user/ble_scanner ;;
  all) fbt updater_package fap_ble_scanner ;;
  *) echo "usage: $0 [fw|app|flash-fw|flash-dfu|flash-app]" >&2; exit 1 ;;
esac
