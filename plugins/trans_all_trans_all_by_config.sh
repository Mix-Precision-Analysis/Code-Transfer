#!/usr/bin/env bash
set -euo pipefail

cc_cmd=${CC:-cc}
# --- 使用数组代替字符串 ---
if pkg-config --exists gsl 2>/dev/null; then
  mapfile -t gsl_flags < <(pkg-config --cflags --libs gsl)
else
  gsl_flags=(
    -I/home/lilei/software/gsl/include
    -L/home/lilei/software/gsl/lib
    -lgsl
    -lgslcblas
    -lm
  )
fi

run_and_time() {
  local exe=$1
  python3 - <<'PY' "$exe"
import subprocess
import sys
import time

exe = sys.argv[1]
start = time.perf_counter()
out = subprocess.check_output([exe]).decode().strip()
elapsed = time.perf_counter() - start
print(f"{out}|{elapsed}")
PY
}

compute_rel_error() {
  python3 - <<'PY' "$1" "$2"
import math
import sys

base = float(sys.argv[1])
val = float(sys.argv[2])
den = abs(base) if base != 0.0 else 1.0
err = abs(val - base) / den
print(err)
PY
}

compute_speedup() {
  python3 - <<'PY' "$1" "$2"
import sys

base_t = float(sys.argv[1])
cur_t = float(sys.argv[2])
print((base_t - cur_t) / base_t if base_t != 0.0 else 0.0)
PY
}

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
stub_header=/home/lilei/workspace/Code-Transfer-Bench/plugins/cleanup_stubs.h

shopt -s nullglob

# THRESHLIST=("1e-4" "1e-6" "1e-8" "1e-10")
THRESHLIST=("1e-4")
for THRESH in "${THRESHLIST[@]}";do
  for bench_dir in "${root_dir}"/bench/copy/*/; do
    bench_name="$(basename "$bench_dir")"
    if [[ "$bench_name" != "Mix-BJ1" ]]; then
      continue
    fi
    base_sources=("$bench_dir"*.c)
    if [[ ${#base_sources[@]} -eq 0 ]]; then
      echo "[skip] $bench_name: no base source .c file in $bench_dir" >&2
      continue
    fi
    base_src="${base_sources[0]}"
    base_exe="${bench_dir%/}/$(basename "${base_src%.c}")-base"

    inc_flags=(-I"$bench_dir" -I"$bench_dir/mixed")
    stub_flags=(
      -include "$stub_header"
      -D__GSL_MATH_H__
      -D__GSL_ERRNO_H__
      -DGSL_MODE_H
      -D_GSL_MODE_H
      -D__GSL_MODE_H__
      -DGSL_SF_TRIG_H
      -D_GSL_SF_TRIG_H
      -D__GSL_SF_TRIG_H__
      -DGSL_SF_RESULT_H
      -D_GSL_SF_RESULT_H
      -D__GSL_SF_RESULT_H__
      -DGSL_SF_EXP_H
      -D_GSL_SF_EXP_H
      -D__GSL_SF_EXP_H__
      -DGSL_SF_AIRY_H
      -D_GSL_SF_AIRY_H
      -D__GSL_SF_AIRY_H__
      -DGSL_SF_EXPINT_H
      -D_GSL_SF_EXPINT_H
      -D__GSL_SF_EXPINT_H__
      -DGSL_SF_ELLINT_H
      -D_GSL_SF_ELLINT_H
      -D__GSL_SF_ELLINT_H__
      -DGSL_SF_LAGUERRE_H
      -D_GSL_SF_LAGUERRE_H
      -D__GSL_SF_LAGUERRE_H__
      -DGSL_SF_LAMBERT_H
      -D_GSL_SF_LAMBERT_H
      -D__GSL_SF_LAMBERT_H__
      -DGSL_SF_LOG_H
      -D_GSL_SF_LOG_H
      -D__GSL_SF_LOG_H__
      -DGSL_SF_GAMMA_H
      -D_GSL_SF_GAMMA_H
      -D__GSL_SF_GAMMA_H__
      -DGSL_SF_SYNCHROTRON_H
      -D_GSL_SF_SYNCHROTRON_H
      -D__GSL_SF_SYNCHROTRON_H__
      -DGSL_SF_BESSEL_H
      -D_GSL_SF_BESSEL_H
      -D__GSL_SF_BESSEL_H__
      -Wno-incompatible-pointer-types
      -Wno-unused-command-line-argument
      -Wno-implicit-function-declaration
      -Wno-implicit-int
      -std=gnu99
    )
    mixed_sources=("$bench_dir"/mixed/*.c)
    if [[ ${#mixed_sources[@]} -eq 0 ]]; then
      echo "[skip] $bench_name: no mixed sources after generation"
      continue
    fi

    echo "==> Building base for $bench_name"
    # 编译时使用 "${gsl_flags[@]}"
    if ! "$cc_cmd" -O2 "${inc_flags[@]}" "$base_src" "${gsl_flags[@]}" -o "$base_exe"; then
      echo "[fail] compile base $bench_name" >&2
      continue
    fi

    if ! base_run=$(run_and_time "$base_exe"); then
      echo "[fail] run base $bench_name" >&2
      continue
    fi
    base_out=${base_run%%|*}
    base_time=${base_run##*|}

    result_md="${bench_dir%/}/result.md"
    {
      echo "| binary | result | time_s | speedup_vs_base | relative_error |"
      echo "| --- | --- | --- | --- | --- |"
      echo "| $(basename "$base_exe") | $base_out | $base_time | 1.0 | 0.0 |"
    } > "$result_md"

    echo "base result: $base_out"
    echo "base time:   $base_time"

    for src in "${mixed_sources[@]}"; do
      exe="${src%.c}"
      echo "-> $(basename "$src")"
      if ! "$cc_cmd" -O2 "${inc_flags[@]}" "${stub_flags[@]}" "$src" "${gsl_flags[@]}" -o "$exe"; then
        echo "[fail] compile $src" >&2
        continue
      fi
      if ! run=$(run_and_time "$exe"); then
        echo "[fail] run $exe" >&2
        continue
      fi
      out=${run%%|*}
      t=${run##*|}
      if ! speedup=$(compute_speedup "$base_time" "$t"); then
        echo "[fail] compute speedup for $exe (base_time=$base_time t=$t)" >&2
        speedup="nan"
      fi

      if [[ -z "${out//[[:space:]]/}" ]]; then
        echo "[fail] empty stdout from $exe" >&2
        rel_err="nan"
      else
        if ! rel_err=$(compute_rel_error "$base_out" "$out"); then
          echo "[fail] compute rel_error for $exe (base=$base_out out=$out)" >&2
          rel_err="nan"
        fi
      fi
      echo "result: $out"
      echo "time:   $t"
      echo "speedup vs base: $speedup"
      echo "relative error:  $rel_err"
      echo "| $(basename "$exe") | $out | $t | $speedup | $rel_err |" >> "$result_md"
      # 可选：删除可执行文件以节省空间
      rm -f "$exe"
    done
    # break
  done
done
