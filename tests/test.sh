#!/usr/bin/env bash
set -Eeuo pipefail

binary=${1:-./udfread}
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
fixture="$root/tests/fixture.udf.xz"
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

xz \
  --decompress \
  --stdout \
  "$fixture" \
  > "$work/fixture.udf"

"$binary" info "$work/fixture.udf" \
  > "$work/info.txt"

grep -Fxq \
  "Volume ID: UDFREAD_TEST" \
  "$work/info.txt"

"$binary" ls -l -R "$work/fixture.udf" \
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
  "$work/fixture.udf" \
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

"$binary" cat \
  "$work/fixture.udf" \
  /hello.txt \
  > "$work/hello.txt"

cmp \
  "$expected/hello.txt" \
  "$work/hello.txt"

"$binary" cat \
  "$work/fixture.udf" \
  /empty.txt \
  > "$work/empty.txt"

test ! -s "$work/empty.txt"

"$binary" extract \
  "$work/fixture.udf" \
  /nested/data.bin \
  "$work/nested.bin"

cmp \
  "$expected/nested.bin" \
  "$work/nested.bin"

"$binary" range \
  -o "$work/range.bin" \
  "$work/fixture.udf" \
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

"$binary" map \
  "$work/fixture.udf" \
  /nested/data.bin \
  > "$work/map.txt"

grep -Fxq \
  $'FILE_BLOCK\tFILE_OFFSET\tLBA\tIMAGE_OFFSET\tBLOCKS\tBYTES' \
  "$work/map.txt"

grep -Fxq \
  $'0\t0\t280\t573440\t3\t6144' \
  "$work/map.txt"

"$binary" blocks \
  "$work/fixture.udf" \
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

echo "All UDF fixture tests passed."
