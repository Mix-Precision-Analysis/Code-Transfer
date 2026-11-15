#!/usr/bin/env bash
set -euo pipefail

# 配置路径
CLANG_BIN=/home/lilei/software/llvm-project/build/bin/clang
PLUGIN=/home/lilei/workspace/Code-Transfer-Bench/plugins/TransformType.so
CLEANUP_PLUGIN=/home/lilei/workspace/Code-Transfer-Bench/plugins/CleanupUnused.so
CLEANUP_NO_INPLACE=${CLEANUP_NO_INPLACE:-0}

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
stub_header=/home/lilei/workspace/Code-Transfer-Bench/plugins/cleanup_stubs.h

shopt -s nullglob
# THRESHLIST=("1e-4" "1e-6" "1e-8" "1e-10")
THRESHLIST=("1e-4")
for THRESH in "${THRESHLIST[@]}";do
  for bench_dir in "$root_dir"/executed/seeds${THRESH}/*/; do
    bench_name="$(basename "$bench_dir")"
    # if [[ "$bench_name" != "Mix-Sum" ]]; then
    #   continue
    # fi
    configs=("$bench_dir"/*.json)
    [[ ${#configs[@]} -eq 0 ]] && { echo "[skip] $bench_name: no configs"; continue; }

    c_candidates=("$bench_dir"/*.c)
    [[ ${#c_candidates[@]} -eq 0 ]] && { echo "[skip] $bench_name: no source .c"; continue; }
    src="${c_candidates[0]}"

    out_dir="${bench_dir%/}/mixed"
    mkdir -p "$out_dir"

    echo "[run] $bench_name -> $(basename "$src") with ${#configs[@]} configs"
    for cfg in "${configs[@]}"; do
      cfg_base="$(basename "$cfg")"
      cfg_base="${cfg_base%.*}"
      src_base="$(basename "$src")"
      ext="${src_base##*.}"
      if [[ "$src_base" == "$ext" ]]; then
        ext=".c"
      else
        ext=".$ext"
      fi
      out_file="$out_dir/mix_${cfg_base}${ext}"

      echo "  → Applying config: $(basename "$cfg")"
      CLANG_BIN="$CLANG_BIN" "$CLANG_BIN" \
        -Xclang -load -Xclang "$PLUGIN" \
        -Xclang -plugin -Xclang trans-type \
        -Xclang -plugin-arg-trans-type -Xclang -output-path \
        -Xclang -plugin-arg-trans-type -Xclang "$out_dir" \
        -Xclang -plugin-arg-trans-type -Xclang -input-config \
        -Xclang -plugin-arg-trans-type -Xclang "$cfg" \
        -I/home/lilei/software/gsl/include \
        "$src" -c

      if [[ -f "$out_file" ]]; then
        echo "  → Cleaning unused functions: $(basename "$out_file")"
        temp_pre="$out_file.pre_cleanup_tmp"
        temp_clean="$out_file.clean_input_tmp"
        temp_out="$out_file.cleanup_tmp"
        cp -f "$out_file" "$temp_pre"
        sed -E '/^[[:space:]]*(static[[:space:]]+|extern[[:space:]]+|inline[[:space:]]+)?[_A-Za-z][_A-Za-z0-9[:space:]\*]*[[:space:]]+gsl_sf_[A-Za-z0-9_]*[[:space:]]*\([^;]*\)[[:space:]]*;[[:space:]]*$/d' "$temp_pre" > "$temp_clean"
        "$CLANG_BIN" \
          -include "$stub_header" \
          -D__GSL_MATH_H__ \
          -D__GSL_ERRNO_H__ \
          -D__GSL_SF_EXP_H__ \
          -D__GSL_SF_AIRY_H__ \
          -DGSL_MODE_H \
          -D_GSL_MODE_H \
          -D__GSL_MODE_H__ \
          -DGSL_SF_RESULT_H \
          -D_GSL_SF_RESULT_H \
          -D__GSL_SF_RESULT_H__ \
          -DGSL_SF_EXPINT_H \
          -D_GSL_SF_EXPINT_H \
          -D__GSL_SF_EXPINT_H__ \
          -DGSL_SF_ELLINT_H \
          -D_GSL_SF_ELLINT_H \
          -D__GSL_SF_ELLINT_H__ \
          -DGSL_SF_BESSEL_H \
          -D_GSL_SF_BESSEL_H \
          -D__GSL_SF_BESSEL_H__ \
          -DGSL_SF_LAGUERRE_H \
          -D_GSL_SF_LAGUERRE_H \
          -D__GSL_SF_LAGUERRE_H__ \
          -DGSL_SF_LAMBERT_H \
          -D_GSL_SF_LAMBERT_H \
          -D__GSL_SF_LAMBERT_H__ \
          -DGSL_SF_LOG_H \
          -D_GSL_SF_LOG_H \
          -D__GSL_SF_LOG_H__ \
          -DGSL_SF_GAMMA_H \
          -D_GSL_SF_GAMMA_H \
          -D__GSL_SF_GAMMA_H__ \
          -DGSL_SUM_H \
          -D_GSL_SUM_H \
          -D__GSL_SUM_H__ \
          -DGSL_SF_SYNCHROTRON_H \
          -D_GSL_SF_SYNCHROTRON_H \
          -D__GSL_SF_SYNCHROTRON_H__ \
          -Wno-incompatible-pointer-types \
          -Wno-unused-value \
          -Wno-return-type \
          -Wno-implicit-function-declaration \
          -Wno-implicit-int \
          -Wno-error=implicit-function-declaration \
          -Wno-error=implicit-int \
          -std=gnu99 \
          -Wno-unused-command-line-argument \
          -Xclang -load -Xclang "$CLEANUP_PLUGIN" \
          -Xclang -plugin -Xclang cleanup-unused \
          -Xclang -plugin-arg-cleanup-unused -Xclang -output-file \
          -Xclang -plugin-arg-cleanup-unused -Xclang "$temp_out" \
          -I/home/lilei/software/gsl/include \
          -I"$bench_dir" \
          -x c "$temp_clean" -fsyntax-only
        rm -f "$temp_pre" "$temp_clean"
        if [[ -f "$temp_out" ]]; then
          if [[ "$CLEANUP_NO_INPLACE" == "0" ]]; then
            mv -f "$temp_out" "$out_file"
          fi
        fi
      else
        echo "  → [skip] no output file: $(basename "$out_file")"
      fi
    done
    # break
  done
done
