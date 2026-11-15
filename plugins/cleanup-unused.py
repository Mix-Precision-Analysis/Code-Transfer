import os
import subprocess
import sys

if len(sys.argv) < 2:
    print("Usage: cleanup-unused.py <file> [temp-dir] [extra clang args...] [--no-inplace]")
    sys.exit(1)

file = sys.argv[1]
temp_dir = ""
extra_args = []
no_inplace = False

if len(sys.argv) >= 3:
    for arg in sys.argv[2:]:
        if arg == "--no-inplace":
            no_inplace = True
        else:
            extra_args.append(arg)
    if extra_args and not extra_args[0].startswith("-"):
        temp_dir = extra_args[0]
        extra_args = extra_args[1:]

filtered_args = []
for arg in extra_args:
    if arg.startswith("-L") or (arg.startswith("-l") and arg != "-lm"):
        continue
    filtered_args.append(arg)

clang_bin = os.environ.get("CLANG_BIN", "clang")
plugin_path = os.path.join(os.path.dirname(__file__), "CleanupUnused.so")
stub_path = os.path.join(os.path.dirname(__file__), "cleanup_stubs.h")

if temp_dir:
    os.makedirs(temp_dir, exist_ok=True)
    temp_output = os.path.join(temp_dir, os.path.basename(file) + ".cleanup_tmp")
else:
    temp_output = file + ".cleanup_tmp"

cmd = [
    clang_bin,
    "-include",
    stub_path,
    "-D__GSL_MATH_H__",
    "-D__GSL_ERRNO_H__",
    "-D__GSL_SF_EXP_H__",
    "-D__GSL_SF_AIRY_H__",
    "-DGSL_MODE_H",
    "-D_GSL_MODE_H",
    "-D__GSL_MODE_H__",
    "-DGSL_SF_RESULT_H",
    "-D_GSL_SF_RESULT_H",
    "-D__GSL_SF_RESULT_H__",
    "-DGSL_SF_EXPINT_H",
    "-D_GSL_SF_EXPINT_H",
    "-D__GSL_SF_EXPINT_H__",
    "-DGSL_SF_ELLINT_H",
    "-D_GSL_SF_ELLINT_H",
    "-D__GSL_SF_ELLINT_H__",
    "-DGSL_SF_BESSEL_H",
    "-D_GSL_SF_BESSEL_H",
    "-D__GSL_SF_BESSEL_H__",
    "-DGSL_SF_LAGUERRE_H",
    "-D_GSL_SF_LAGUERRE_H",
    "-D__GSL_SF_LAGUERRE_H__",
    "-DGSL_SF_LAMBERT_H",
    "-D_GSL_SF_LAMBERT_H",
    "-D__GSL_SF_LAMBERT_H__",
    "-DGSL_SF_LOG_H",
    "-D_GSL_SF_LOG_H",
    "-D__GSL_SF_LOG_H__",
    "-DGSL_SF_GAMMA_H",
    "-D_GSL_SF_GAMMA_H",
    "-D__GSL_SF_GAMMA_H__",
    "-DGSL_SUM_H",
    "-D_GSL_SUM_H",
    "-D__GSL_SUM_H__",
    "-DGSL_SF_SYNCHROTRON_H",
    "-D_GSL_SF_SYNCHROTRON_H",
    "-D__GSL_SF_SYNCHROTRON_H__",
    "-Wno-incompatible-pointer-types",
    "-Wno-unused-value",
    "-Wno-return-type",
    "-Wno-implicit-function-declaration",
    "-Wno-implicit-int",
    "-Wno-error=implicit-function-declaration",
    "-Wno-error=implicit-int",
    "-std=gnu99",
    "-Xclang",
    "-load",
    "-Xclang",
    plugin_path,
    "-Xclang",
    "-plugin",
    "-Xclang",
    "cleanup-unused",
    "-Xclang",
    "-plugin-arg-cleanup-unused",
    "-Xclang",
    "-output-file",
    "-Xclang",
    "-plugin-arg-cleanup-unused",
    "-Xclang",
    temp_output,
]

cmd += filtered_args + [file, "-fsyntax-only"]

print("Starting unused function cleanup...")
result = subprocess.run(cmd, check=False)
if not no_inplace and os.path.exists(temp_output):
    os.replace(temp_output, file)
if result.returncode != 0:
    print("Cleanup finished with clang errors; output may still be generated.")
print("Done.")
