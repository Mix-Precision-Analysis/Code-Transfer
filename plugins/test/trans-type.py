import os
import subprocess
import sys

file = sys.argv[1]
CONFIG = sys.argv[2]

ROOT = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(ROOT, "..", ".."))
CLANG = "/opt/homebrew/opt/llvm/bin/clang"
PLUGIN = os.path.abspath(os.path.join(ROOT, "..", "TransformType.so"))
OUT_DIR = os.path.join(ROOT, "out")
GSL_INCLUDE = "/opt/homebrew/opt/gsl/include"
GSL_LIB = "/opt/homebrew/opt/gsl/lib"
MIX_LOG_INCLUDE = os.path.join(REPO_ROOT, "Mix-BJ1")

cmd = [
    CLANG,
    "-Xclang", "-load", "-Xclang", PLUGIN,
    "-Xclang", "-plugin", "-Xclang", "trans-type",
    "-Xclang", "-plugin-arg-trans-type", "-Xclang", "-output-path",
    "-Xclang", "-plugin-arg-trans-type", "-Xclang", OUT_DIR,
    "-Xclang", "-plugin-arg-trans-type", "-Xclang", "-input-config",
    "-Xclang", "-plugin-arg-trans-type", "-Xclang", CONFIG,
    "-I" + GSL_INCLUDE,
    "-I" + MIX_LOG_INCLUDE,
    "-L" + GSL_LIB,
    "-lgsl",
    "-lgslcblas",
    "-lm",
    file,
    "-c",  # 需要产出对象文件才能触发重写；如果不想生成 .o，可以保留 -c
]

print("Starting source-to-source transformation...")
subprocess.run(cmd, check=True)
print("Done. Transformed file should be in ./out/<原文件名>")
