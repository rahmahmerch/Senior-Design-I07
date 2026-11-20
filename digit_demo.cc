/***************************************************************************//**
 * @file digit_demo.cc
 * @brief MVP accelerator demo for the I07 IMU pen. See digit_demo.h.
 ******************************************************************************/
#include "digit_demo.h"
#include "test_vectors.h"
#include "sl_ml_model_digit_cnn.h"
#include "sl_sleeptimer.h"
#include "sl_status.h"
#include "sl_led.h"
#include "sl_simple_led_instances.h"
#include "em_device.h"
#include <cinttypes>
#include <cstdio>
#include <cstring>

#ifndef DIGIT_DEMO_KERNELS
#define DIGIT_DEMO_KERNELS "unknown"
#endif

#define VECTORS_PER_CALL 8

static bool model_ok = false;
static volatile bool run_requested = false;
static bool running = false;
static int next_vector = 0;

// Results of the current pass
static int correct = 0;
static int confusion[TV_NUM_CLASSES][TV_NUM_CLASSES];
static uint32_t t_min_us, t_max_us;
static uint64_t t_sum_us;
static uint32_t cyc_min, cyc_max;
static uint64_t cyc_sum;
static uint32_t pass_start_ms;
static uint32_t core_hz;

// --- cycle counter -----------------------------------------------------------
static void cyccnt_init(void)
{
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CYCCNT = 0;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

static inline uint32_t cyccnt(void)
{
  return DWT->CYCCNT;
}

// --- helpers -----------------------------------------------------------------
static void reset_stats(void)
{
  correct = 0;
  memset(confusion, 0, sizeof(confusion));
  t_min_us = UINT32_MAX; t_max_us = 0; t_sum_us = 0;
  cyc_min = UINT32_MAX;  cyc_max = 0;  cyc_sum = 0;
}

static uint32_t now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
}

// --- public API --------------------------------------------------------------
void digit_demo_init(void)
{
  printf("\r\n=== I07 digit CNN on %s kernels ===\r\n", DIGIT_DEMO_KERNELS);

  if (sl_ml_model_init(&sl_ml_digit_cnn_model_handle) != SL_STATUS_OK) {
    printf("error: model init failed\r\n");
    return;
  }
  TfLiteTensor* in  = sl_ml_digit_cnn_model_handle.input_tensor(0);
  TfLiteTensor* out = sl_ml_digit_cnn_model_handle.output_tensor(0);
  if (in == nullptr || out == nullptr) {
    printf("error: tensors unavailable\r\n");
    return;
  }
  if (in->dims->size != 4 || in->dims->data[0] != 1
      || in->dims->data[1] != TV_WINDOW || in->dims->data[2] != TV_CHANNELS
      || in->type != kTfLiteInt8) {
    printf("error: unexpected input tensor (dims=%d [%d,%d,%d,%d] type=%d)\r\n",
           in->dims->size, in->dims->data[0], in->dims->data[1],
           in->dims->data[2], in->dims->size > 3 ? in->dims->data[3] : -1, in->type);
    return;
  }
  if (out->dims->data[out->dims->size - 1] != TV_NUM_CLASSES || out->type != kTfLiteInt8) {
    printf("error: unexpected output tensor\r\n");
    return;
  }

  core_hz = SystemCoreClockGet();
  cyccnt_init();

  int32_t arena_total = 0;
  for (int i = 0; i < sl_ml_digit_cnn_model_handle.buffer_count; i++) {
    arena_total += sl_ml_digit_cnn_model_handle.buffer_sizes[i];
  }
  printf("model: %d bytes flatbuffer, %" PRId32 " bytes tensor arena in %" PRId32 " buffer(s)\r\n",
         sl_ml_digit_cnn_model_handle.flatbuffer_length, arena_total,
         sl_ml_digit_cnn_model_handle.buffer_count);
  // scales printed as integers (x1e-6) so float printf support is not needed
  printf("input: int8 [1,%d,%d,1] scale=%" PRId32 "e-6 zp=%d   output: int8 [%d] scale=%" PRId32 "e-6 zp=%d\r\n",
         TV_WINDOW, TV_CHANNELS, (int32_t)(in->params.scale * 1e6f + 0.5f), (int)in->params.zero_point,
         TV_NUM_CLASSES, (int32_t)(out->params.scale * 1e6f + 0.5f), (int)out->params.zero_point);
  printf("test set: %d vectors, %d classes, core clock %" PRIu32 " Hz\r\n",
         TV_NUM_VECTORS, TV_NUM_CLASSES, core_hz);
  printf("press BTN0 to run\r\n");
  model_ok = true;
}

void digit_demo_request_run(void)
{
  run_requested = true;
}

bool digit_demo_is_running(void)
{
  return running;
}

static void run_one(int idx)
{
  TfLiteTensor* in  = sl_ml_digit_cnn_model_handle.input_tensor(0);
  TfLiteTensor* out = sl_ml_digit_cnn_model_handle.output_tensor(0);

  memcpy(in->data.int8, tv_data[idx], TV_WINDOW * TV_CHANNELS);

  uint32_t t0 = sl_sleeptimer_get_tick_count();
  uint32_t c0 = cyccnt();
  sl_status_t st = sl_ml_model_run(&sl_ml_digit_cnn_model_handle);
  uint32_t c1 = cyccnt();
  uint32_t t1 = sl_sleeptimer_get_tick_count();

  if (st != SL_STATUS_OK) {
    printf("v=%3d inference failed (0x%lx)\r\n", idx, (unsigned long)st);
    return;
  }

  // Prefer the cycle counter; fall back to the sleeptimer if DWT is dead.
  uint32_t cycles = c1 - c0;
  uint32_t us;
  if (cycles > 0 && core_hz > 0) {
    us = (uint32_t)(((uint64_t)cycles * 1000000ULL) / core_hz);
  } else {
    us = (uint32_t)(((uint64_t)(t1 - t0) * 1000000ULL) / sl_sleeptimer_get_timer_frequency());
  }

  int best = 0;
  for (int c = 1; c < TV_NUM_CLASSES; c++) {
    if (out->data.int8[c] > out->data.int8[best]) best = c;
  }
  // int8 softmax -> probability
  float prob = ((float)out->data.int8[best] - (float)out->params.zero_point) * out->params.scale;
  int label = tv_labels[idx];
  bool ok = (best == label);

  confusion[label][best]++;
  if (ok) correct++;
  if (us < t_min_us) t_min_us = us;
  if (us > t_max_us) t_max_us = us;
  t_sum_us += us;
  if (cycles < cyc_min) cyc_min = cycles;
  if (cycles > cyc_max) cyc_max = cycles;
  cyc_sum += cycles;

  printf("v=%3d label=%d pred=%d conf=%3d%% t=%5" PRIu32 " us %s\r\n",
         idx, label, best, (int)(prob * 100.0f + 0.5f), us, ok ? "ok" : "MISS");
}

static void print_summary(void)
{
  uint32_t elapsed_ms = now_ms() - pass_start_ms;
  printf("\r\n--- summary (%s kernels) ---\r\n", DIGIT_DEMO_KERNELS);
  printf("accuracy: %d/%d = %d.%d%%\r\n", correct, TV_NUM_VECTORS,
         (correct * 1000 / TV_NUM_VECTORS) / 10, (correct * 1000 / TV_NUM_VECTORS) % 10);
  printf("inference time us: min %" PRIu32 "  avg %" PRIu32 "  max %" PRIu32 "\r\n",
         t_min_us, (uint32_t)(t_sum_us / TV_NUM_VECTORS), t_max_us);
  printf("inference cycles:  min %" PRIu32 "  avg %" PRIu32 "  max %" PRIu32 " (core %" PRIu32 " Hz)\r\n",
         cyc_min, (uint32_t)(cyc_sum / TV_NUM_VECTORS), cyc_max, core_hz);
  printf("per class correct:");
  for (int c = 0; c < TV_NUM_CLASSES; c++) {
    int n = 0;
    for (int p = 0; p < TV_NUM_CLASSES; p++) n += confusion[c][p];
    printf(" %d:%d/%d", c, confusion[c][c], n);
  }
  printf("\r\nconfusion (rows=true, cols=pred):\r\n");
  for (int c = 0; c < TV_NUM_CLASSES; c++) {
    printf("  %d |", c);
    for (int p = 0; p < TV_NUM_CLASSES; p++) printf(" %2d", confusion[c][p]);
    printf("\r\n");
  }
  printf("pass wall time: %" PRIu32 " ms (includes UART printing)\r\n", elapsed_ms);
  printf("--- end ---\r\n\r\n");
}

void digit_demo_process(void)
{
  if (!model_ok) return;

  if (!running && run_requested) {
    run_requested = false;
    running = true;
    next_vector = 0;
    reset_stats();
    pass_start_ms = now_ms();
    sl_led_turn_on(&sl_led_led1);
    printf("\r\n--- running %d test vectors ---\r\n", TV_NUM_VECTORS);
  }
  if (!running) return;

  for (int n = 0; n < VECTORS_PER_CALL && next_vector < TV_NUM_VECTORS; n++) {
    run_one(next_vector++);
  }
  if (next_vector >= TV_NUM_VECTORS) {
    running = false;
    run_requested = false;
    sl_led_turn_off(&sl_led_led1);
    print_summary();
  }
}
