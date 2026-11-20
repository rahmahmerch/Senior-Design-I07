#!/bin/bash
# Build the IMU_Pen project from the terminal (no Simplicity Studio GUI needed).
#
#   ./build_imu_pen.sh          -> mvp    : MVP accelerated kernels (default)
#   ./build_imu_pen.sh cmsis    -> cmsis  : CPU, CMSIS-NN optimized kernels
#   ./build_imu_pen.sh sw_ref   -> sw_ref : CPU, plain reference C kernels
# The model compiler target (accelerator id) is switched to match.
#
# Output: build_cli_imu_pen_<variant>/cmake_gcc/build/base/imu_pen.{s37,hex,out}
# Requires the Silicon Labs tools installed by the Simplicity Installer under ~/.silabs.
set -euo pipefail
cd "$(dirname "$0")"

VARIANT="${1:-mvp}"
case "$VARIANT" in
  mvp)    KERNEL_COMPONENT=tensorflow_lite_micro_accelerated_kernels; ACCEL=mvpv1;  LABEL="MVP accelerated" ;;
  cmsis)  KERNEL_COMPONENT=tensorflow_lite_micro_optimized_kernels;   ACCEL=cmsis;  LABEL="CPU CMSIS-NN" ;;
  sw_ref) KERNEL_COMPONENT=tensorflow_lite_micro_reference_kernels;   ACCEL=sw_ref; LABEL="CPU reference" ;;
  *) echo "usage: $0 [mvp|cmsis|sw_ref]"; exit 2 ;;
esac
TOOLS="$HOME/.silabs/tools.json"
pick() { python3 -c "import json,sys;print(json.load(open('$TOOLS'))['$1'][0]['path'])"; }
SDK=$(pick simplicity-sdk)
AIML=$(pick aiml)
SLC="$(pick slc-cli)/slc"
GCC_BIN="$(dirname "$(find "$HOME/.silabs/slt/installs/conan/p" -maxdepth 4 -name arm-none-eabi-gcc | head -1)")"
CMAKE_BIN="$(dirname "$(find "$HOME/.silabs/slt/installs/conan/p" -maxdepth 7 -type f -name cmake -perm -u+x | head -1)")"
NINJA_BIN="$(dirname "$(find "$HOME/.silabs/slt/installs/conan/p" -maxdepth 7 -type f -name ninja -perm -u+x | head -1)")"
export PATH="$CMAKE_BIN:$NINJA_BIN:$GCC_BIN:$PATH"

OUT="$PWD/build_cli_imu_pen_$VARIANT"
SLCP="IMU_Pen_$VARIANT.slcp"
# Same project; only the kernel component and the label the firmware prints differ.
sed -e "s/id: tensorflow_lite_micro_accelerated_kernels/id: $KERNEL_COMPONENT/" \
    -e "s/value: '\"MVP accelerated\"'/value: '\"$LABEL\"'/" \
    IMU_Pen.slcp > "$SLCP"

echo "== generating ($VARIANT kernels) into $OUT"
rm -rf "$OUT"; mkdir -p "$OUT"
# Seed with the project's edited config files (GATT database, model config,
# pin and driver configs) so slc preserves them instead of writing templates.
cp -R config "$OUT/config"
# The graph and memory optimizers are MVP-only features of the model compiler.
OPT=false; [ "$ACCEL" = "mvpv1" ] && OPT=true
cat > "$OUT/config/tflite/digit_cnn.mlconf" <<MLCONF
model: digit_cnn.tflite
accelerator: $ACCEL

graph_optimizer:
  enabled: $OPT

memory_optimizer:
  enabled: $OPT
MLCONF
"$SLC" generate -p "$SLCP" -d "$OUT" --sdk-package-path "$SDK,$AIML" --tt -o cmake -name imu_pen \
  > "$OUT/slc_generate.log" 2>&1 || { grep -E 'did not succeed|compile:|Problems|error' "$OUT/slc_generate.log"; exit 1; }
grep -q 'did not succeed' "$OUT/slc_generate.log" && { grep -E 'compile:|Problems|error' "$OUT/slc_generate.log"; exit 1; }
rm -f "$SLCP"

echo "== building"
cd "$OUT/cmake_gcc"
cmake --preset project > cmake_configure.log 2>&1
cmake --build --preset default_config -- -j8 > cmake_build.log 2>&1 || { grep -E 'error|undefined' cmake_build.log | head -20; exit 1; }
echo "== done: $OUT/cmake_gcc/build/base/imu_pen.s37"
arm-none-eabi-size build/base/imu_pen.out
