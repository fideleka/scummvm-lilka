#!/usr/bin/env bash
# Reproducibly build every implemented Lilka engine from isolated source trees.
# Requires ESP-IDF 5.3.2 (IDF_PATH or ../esp/esp-idf); never flashes a device.
set -euo pipefail

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
idf_dir="${IDF_PATH:-$repo_dir/../esp/esp-idf}"
engines=(scumm kyra gob)
if (( $# > 1 )); then
    echo "Usage: $0 [scumm|kyra|gob]" >&2
    exit 2
elif (( $# == 1 )); then
    case "$1" in
        scumm|kyra|gob) engines=("$1") ;;
        *) echo "Usage: $0 [scumm|kyra|gob]" >&2; exit 2 ;;
    esac
fi
if [[ ! -f "$idf_dir/export.sh" ]]; then
    echo "ESP-IDF not found at $idf_dir; set IDF_PATH to ESP-IDF 5.3.2" >&2
    exit 1
fi
idf_dir="$(cd "$idf_dir" && pwd)"

output_dir="$repo_dir/build/lilka-engines"
mkdir -p "$output_dir"
temp_dir=""
cleanup() {
    if [[ -n "$temp_dir" ]]; then
        git -C "$repo_dir" -c "safe.directory=$repo_dir" worktree remove --force "$temp_dir/source" 2>/dev/null || true
        rmdir "$temp_dir" 2>/dev/null || true
    fi
}
trap cleanup EXIT

artifacts=()
for engine in "${engines[@]}"; do
    temp_dir="$(mktemp -d "${TMPDIR:-/tmp}/lilka-$engine.XXXXXXXX")"
    git -C "$repo_dir" -c "safe.directory=$repo_dir" worktree add --detach --quiet "$temp_dir/source" HEAD
    echo "Building $engine from $(git -C "$repo_dir" -c "safe.directory=$repo_dir" rev-parse --short HEAD)"
    (cd "$temp_dir/source" && IDF_PATH="$idf_dir" ./compile-lilka.sh "$engine")
    cp -f "$temp_dir/source/backends/platform/esp32/build/$engine.bin" "$output_dir/$engine.bin"
    artifacts+=("$output_dir/$engine.bin")
    cleanup
    temp_dir=""
done

if (( $# == 0 )) || [[ "$1" == kyra ]]; then
    cp -f "$repo_dir/dists/engine-data/kyra.dat" "$output_dir/kyra.dat"
    artifacts+=("$output_dir/kyra.dat")
fi
echo "Batch output: $output_dir"
wc -c "${artifacts[@]}"
sha256sum "${artifacts[@]}"
