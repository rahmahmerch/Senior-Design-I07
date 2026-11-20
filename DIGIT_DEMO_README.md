# MVP accelerator demo: int8 digit CNN on the xG26 (branch `mvp_digit_demo`)

Runs a 10-class digit classifier on the EFR32MG26's MVP (Matrix Vector Processor)
over 200 synthetic IMU windows baked into flash, then reports predictions, timing
and accuracy over UART. Same window format, model and inference call the pen will
use with live IMU data; only the data source differs.

## Files
- `digit_demo.cc / .h`   demo module (model init, test loop, timing, summary)
- `test_vectors.h`       200 int8 windows (128 samples x 6 channels) with labels
- `config/tflite/digit_cnn.tflite`  int8 model (Conv2D, MaxPool, Conv2D, MaxPool, Dense, Dense, Softmax)
- `build_imu_pen.sh`     terminal build: `./build_imu_pen.sh [mvp|cmsis|sw_ref]`
- `digit_demo_mvp_log.txt`, `digit_demo_cmsis_log.txt`  captured UART logs
- Model and data come from `~/Downloads/i07_synthetic_imu/` (generator + training script).

## Run
1. `./build_imu_pen.sh` (MVP) or `./build_imu_pen.sh cmsis` (CPU).
2. Flash: `commander flash build_cli_imu_pen_<variant>/cmake_gcc/build/base/imu_pen.s37 --device EFR32MG26B510F3200IM68`
3. Serial console at 115200 baud. Press BTN0. LED1 is on while the 200 vectors run.

## Results, 2026-11-18, BRD2608A at 39 MHz core clock
| | MVP accelerated | CPU, CMSIS-NN kernels |
|---|---|---|
| Model compiler target | mvpv1 | cmsis |
| Accuracy, 200 held-out vectors | 193/200 = 96.5 % | 194/200 = 97.0 % |
| Inference time, min / avg / max | 507 / 613 / 1302 us | 18146 / 18323 / 19004 us |
| Cycles, avg | 23,941 | 714,632 |
| Speedup, avg | 29.9x | 1x |
| Tensor arena | 37,911 B (incl. 32 KB paging buffer) | 4,967 B |
| Flatbuffer in flash | 21,376 B | 19,768 B |
| Firmware text | 452 KB (incl. BLE stack, 154 KB test vectors) | 499 KB |

Notes for the report
- Laptop int8 accuracy on the same vectors is 97.0 %. The CPU build matches it
  exactly. The MVP build is 0.5 points lower because the MVP computes internally
  in 16-bit float (documented by Silicon Labs). Same confusions in both: 6 -> 3, 9 -> 0/5.
- Conv2D, MaxPool2D and FullyConnected run on the MVP; Reshape and Softmax run on
  the Cortex-M33 in both builds.
- The data is synthetic (stroke geometry -> simulated accel/gyro). The demo
  validates the accelerator pipeline and resource footprint, not handwriting
  accuracy. Retrain on recorded pen data with the same training script later.
