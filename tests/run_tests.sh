#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/test-build"
CXX="${CXX:-g++}"
AS="${AS:-riscv64-linux-gnu-as}"
LD="${LD:-riscv64-linux-gnu-ld}"
QEMU="${QEMU:-qemu-riscv32}"

mkdir -p "$BUILD"

echo "[build] ToyC compiler"
"$CXX" -std=c++20 -O2 -Wall -Wextra -Wpedantic \
  "$ROOT/src/main.cpp" -o "$BUILD/compiler"

echo "[build] RV32 test runtime"
"$AS" -march=rv32im -mabi=ilp32 "$ROOT/tests/runtime.S" \
  -o "$BUILD/runtime.o"

passed=0
failed=0

for mode in baseline opt; do
  echo "[test] $mode"
  args=()
  [[ "$mode" == "opt" ]] && args=(-opt)

  for source in "$ROOT"/tests/*.tc; do
    name="$(basename "$source" .tc)"
    expected="$(tr -d '[:space:]' < "$ROOT/tests/$name.expected")"
    artifact="${name}_${mode}"

    "$BUILD/compiler" "${args[@]}" < "$source" > "$BUILD/$artifact.s"
    "$AS" -march=rv32im -mabi=ilp32 "$BUILD/$artifact.s" \
      -o "$BUILD/$artifact.o"
    "$LD" -m elf32lriscv -e _start \
      "$BUILD/runtime.o" "$BUILD/$artifact.o" -o "$BUILD/$artifact"

    set +e
    "$QEMU" "$BUILD/$artifact"
    actual=$?
    set -e

    if [[ "$actual" == "$expected" ]]; then
      printf '[pass] %-20s expected=%s actual=%s\n' "$artifact" "$expected" "$actual"
      passed=$((passed + 1))
    else
      printf '[fail] %-20s expected=%s actual=%s\n' "$artifact" "$expected" "$actual"
      failed=$((failed + 1))
    fi
  done
done

echo
echo "Result: $passed passed, $failed failed"
[[ "$failed" -eq 0 ]]
