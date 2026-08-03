#!/usr/bin/env bash
set -Eeuo pipefail

binary=${1:-./udfread}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
fixture="$root/tests/fixture.iso"
checksum="$root/tests/fixture.iso.sha256"
expected="$root/tests/expected"
work=$(mktemp -d)

cleanup() {
  rm -rf "$work"
}

trap cleanup EXIT

if [ ! -x "$binary" ]; then
  echo "ERROR: Executable not found: $binary" >&2
  exit 1
fi

(
  cd "$root/tests"
  sha256sum --check "$(basename "$checksum")"
)

"$binary" info "$fixture" \
  > "$work/info.txt"

grep -Fxq \
  "Volume ID: UDFREAD_TEST" \
  "$work/info.txt"

"$binary" ls -l -R "$fixture" \
  > "$work/list.txt"

grep -Fxq -- \
  "-            0 /empty.txt" \
  "$work/list.txt"

grep -Fxq -- \
  "-           28 /hello.txt" \
  "$work/list.txt"

grep -Fxq \
  "d            - /nested" \
  "$work/list.txt"

grep -Fxq -- \
  "-         6144 /nested/data.bin" \
  "$work/list.txt"

"$binary" stat \
  "$fixture" \
  /nested/data.bin \
  > "$work/stat.txt"

grep -Fxq \
  "Type: regular file" \
  "$work/stat.txt"

grep -Fxq \
  "Size: 6144 bytes" \
  "$work/stat.txt"

grep -Fxq \
  "First LBA: 280" \
  "$work/stat.txt"

if "$binary" stat \
    "$fixture" \
    /NESTED/DATA.BIN \
    > "$work/stat-sensitive.txt" \
    2> "$work/stat-sensitive.err"; then
  echo "ERROR: Case-sensitive lookup unexpectedly succeeded." >&2
  exit 1
fi

grep -Fxq \
  "udfread: path not found: /NESTED/DATA.BIN" \
  "$work/stat-sensitive.err"

"$binary" stat \
  --ignore-case \
  "$fixture" \
  /NESTED/DATA.BIN \
  > "$work/stat-insensitive.txt"

grep -Fxq \
  "Path: /nested/data.bin" \
  "$work/stat-insensitive.txt"

"$binary" ls \
  -i \
  "$fixture" \
  /NESTED \
  > "$work/list-insensitive.txt"

grep -Fxq \
  "/nested/data.bin" \
  "$work/list-insensitive.txt"

"$binary" cat \
  "$fixture" \
  /hello.txt \
  > "$work/hello.txt"

cmp \
  "$expected/hello.txt" \
  "$work/hello.txt"

"$binary" cat \
  --ignore-case \
  "$fixture" \
  /HELLO.TXT \
  > "$work/hello-insensitive.txt"

cmp \
  "$expected/hello.txt" \
  "$work/hello-insensitive.txt"

"$binary" cat \
  -i \
  "$fixture" \
  /Case.txt \
  > "$work/upper-case.txt"

cmp \
  "$expected/upper-case.txt" \
  "$work/upper-case.txt"

"$binary" cat \
  -i \
  "$fixture" \
  /case.txt \
  > "$work/lower-case.txt"

cmp \
  "$expected/lower-case.txt" \
  "$work/lower-case.txt"

if "$binary" cat \
    -i \
    "$fixture" \
    /CASE.TXT \
    > "$work/ambiguous.txt" \
    2> "$work/ambiguous.err"; then
  echo "ERROR: Ambiguous case-insensitive lookup unexpectedly succeeded." >&2
  exit 1
fi

grep -Fxq \
  "udfread: ambiguous case-insensitive path component 'CASE.TXT' in '/'" \
  "$work/ambiguous.err"

"$binary" cat \
  "$fixture" \
  /empty.txt \
  > "$work/empty.txt"

test ! -s "$work/empty.txt"

"$binary" extract \
  "$fixture" \
  /nested/data.bin \
  "$work/nested.bin"

cmp \
  "$expected/nested.bin" \
  "$work/nested.bin"

"$binary" extract \
  -i \
  "$fixture" \
  /NESTED/DATA.BIN \
  "$work/nested-insensitive.bin"

cmp \
  "$expected/nested.bin" \
  "$work/nested-insensitive.bin"

"$binary" range \
  -o "$work/range.bin" \
  "$fixture" \
  /nested/data.bin \
  1900 \
  500

dd \
  if="$expected/nested.bin" \
  of="$work/expected-range.bin" \
  iflag=skip_bytes,count_bytes \
  skip=1900 \
  count=500 \
  status=none

cmp \
  "$work/expected-range.bin" \
  "$work/range.bin"

"$binary" range \
  -o "$work/range-insensitive.bin" \
  -i \
  "$fixture" \
  /NESTED/DATA.BIN \
  1900 \
  500

cmp \
  "$work/expected-range.bin" \
  "$work/range-insensitive.bin"

"$binary" map \
  "$fixture" \
  /nested/data.bin \
  > "$work/map.txt"

grep -Fxq \
  $'FILE_BLOCK\tFILE_OFFSET\tLBA\tIMAGE_OFFSET\tBLOCKS\tBYTES' \
  "$work/map.txt"

grep -Fxq \
  $'0\t0\t280\t573440\t3\t6144' \
  "$work/map.txt"

"$binary" map \
  -i \
  "$fixture" \
  /NESTED/DATA.BIN \
  > "$work/map-insensitive.txt"

cmp \
  "$work/map.txt" \
  "$work/map-insensitive.txt"

"$binary" blocks \
  "$fixture" \
  /nested/data.bin \
  0 \
  2 \
  > "$work/blocks.bin"

dd \
  if="$expected/nested.bin" \
  of="$work/expected-blocks.bin" \
  count=2 \
  bs=2048 \
  status=none

cmp \
  "$work/expected-blocks.bin" \
  "$work/blocks.bin"

"$binary" blocks \
  -i \
  "$fixture" \
  /NESTED/DATA.BIN \
  0 \
  2 \
  > "$work/blocks-insensitive.bin"

cmp \
  "$work/expected-blocks.bin" \
  "$work/blocks-insensitive.bin"

echo "All UDF fixture tests passed."
