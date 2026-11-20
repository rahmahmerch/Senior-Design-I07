/***************************************************************************//**
 * @file digit_demo.h
 * @brief MVP accelerator demo: run an int8 digit CNN over baked-in synthetic
 *        IMU windows and report predictions, timing and accuracy over UART.
 *
 * The data path is identical to the live pen pipeline: a 128 x 6 int8 window
 * goes into the model, a 10-way softmax comes out. Only the source of the
 * window differs (flash array now, IMU ring buffer later).
 ******************************************************************************/
#ifndef DIGIT_DEMO_H
#define DIGIT_DEMO_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Load the model, check the tensor layout, print model info. Call once. */
void digit_demo_init(void);

/** Request a full pass over the test vectors (safe from an ISR / button). */
void digit_demo_request_run(void);

/** Call from the main loop. Processes a few vectors per call so the
 *  Bluetooth stack keeps getting serviced. */
void digit_demo_process(void);

/** True while a pass is in progress. */
bool digit_demo_is_running(void);

#ifdef __cplusplus
}
#endif

#endif // DIGIT_DEMO_H
