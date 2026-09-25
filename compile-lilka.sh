#!/usr/bin/env bash
# Build the raw Lilka guest application; never flash a device.
set -e

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$repo_dir/backends/platform/esp32"
idf_dir="${IDF_PATH:-$repo_dir/../esp/esp-idf}"

if [[ ! -f "$idf_dir/export.sh" ]]; then
    echo "ESP-IDF not found at: $idf_dir" >&2
    echo "Place it at ../esp/esp-idf relative to this repo, or set IDF_PATH." >&2
    exit 1
fi

for tool in cmake ninja; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "Missing $tool on PATH. Install it in the build environment (macOS: brew; Ubuntu/WSL: apt)." >&2
        exit 1
    fi
done

# export.sh configures the ESP-IDF Python environment and toolchain.
# shellcheck disable=SC1090
source "$idf_dir/export.sh"

if ! command -v idf.py >/dev/null 2>&1; then
    echo "idf.py is unavailable after loading $idf_dir/export.sh" >&2
    exit 1
fi

idf_version="$(idf.py --version)"
if [[ "$idf_version" != *"v5.3.2"* ]]; then
    echo "Expected ESP-IDF v5.3.2; found: $idf_version" >&2
    exit 1
fi

cd "$project_dir"
idf.py build

image="$project_dir/build/scummvm.bin"
if [[ ! -f "$image" ]]; then
    echo "Build finished without the expected raw application image: $image" >&2
    exit 1
fi

image_bytes="$(wc -c < "$image")"
slot_bytes=$((0x640000))
if (( image_bytes > slot_bytes )); then
    echo "Image is too large for Keira app1: $image_bytes > $slot_bytes bytes" >&2
    exit 1
fi

echo "Raw Lilka guest: $image"
echo "Size: $image_bytes / $slot_bytes bytes (free: $((slot_bytes - image_bytes)))"

windows_copy_dir="/mnt/d/Software/scummvm-lilka"
if [[ -d "$windows_copy_dir" ]]; then
    windows_copy="$windows_copy_dir/scummvm.bin"
    cp -f "$image" "$windows_copy"
    echo "Windows copy: $windows_copy"
fi

echo "Copy this application image to the SD card as scummvm/engines/scumm.bin."
