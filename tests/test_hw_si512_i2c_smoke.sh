#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
module_root="$(cd "$script_dir/.." && pwd)"
artifact_dir="${SROBOTIS_TEST_ARTIFACT_DIR:-${SROBOTIS_OUTPUT_ROOT:-$PWD/output}/test-artifacts/components/peripherals/nfc/${SROBOTIS_TEST_NAME:-nfc-si512-i2c-hardware-smoke}}"
log_dir="$artifact_dir/logs"
build_dir="$artifact_dir/build"
log_file="$log_dir/nfc_si512_i2c_hardware_smoke.log"

dev_path="${NFC_I2C_DEV:-/dev/i2c-5}"
addr="${NFC_I2C_ADDR:-0x28}"
block="${NFC_TEST_BLOCK:-4}"
timeout_s="${NFC_HW_SMOKE_TIMEOUT_S:-15}"

mkdir -p "$log_dir" "$build_dir"

{
    echo "[info] module_root=$module_root"
    echo "[info] build_dir=$build_dir"
    echo "[info] dev_path=$dev_path"
    echo "[info] addr=$addr"
    echo "[info] block=$block"
    echo "[info] timeout_s=$timeout_s"

    cmake -S "$module_root" -B "$build_dir" \
        -DBUILD_TESTS=ON \
        -DSROBOTIS_PERIPHERALS_NFC_ENABLED_DRIVERS=drv_i2c_SI512

    cmake --build "$build_dir" --target test_nfc_i2c -j"$(nproc)"

    LD_LIBRARY_PATH="$build_dir:${LD_LIBRARY_PATH:-}" \
        timeout "$timeout_s" "$build_dir/test_nfc_i2c" "$dev_path" "$addr" "$block"
} 2>&1 | tee "$log_file"

if grep -q "\\[NFC-SI512\\] warn:" "$log_file"; then
    echo "[error] SI512 driver reported an I2C open/ioctl warning" | tee -a "$log_file"
    exit 1
fi

grep -q "\\[poll\\] tag detected" "$log_file"
