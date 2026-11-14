/***************************************************************************//**
 * @file
 * @brief Core application logic.
 *******************************************************************************
 * # License
 * <b>Copyright 2024 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: Zlib
 *
 * The licensor of this software is Silicon Laboratories Inc.
 *
 * This software is provided 'as-is', without any express or implied
 * warranty. In no event will the authors be held liable for any damages
 * arising from the use of this software.
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software. If you use this software
 *    in a product, an acknowledgment in the product documentation would be
 *    appreciated but is not required.
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.
 * 3. This notice may not be removed or altered from any source distribution.
 *
 ******************************************************************************/
#include "sl_bt_api.h"
#include "sl_main_init.h"
#include "app_assert.h"
#include "app.h"
#include "sl_board_control.h"
#include "sl_imu.h"
#include "app_log.h"
#include "sl_bluetooth.h"
#include "sl_simple_led_instances.h"
#include "sl_simple_button_instances.h"
#include "stdio.h"
#include "stdbool.h"
#include "gatt_db.h"

// The advertising set handle allocated from Bluetooth stack.
static uint8_t advertising_set_handle = 0xff;
static uint32_t loop_counter = 0;
static bool notify_enabled = false;
// Set by BTN0 (start) / BTN1 (stop). Default: streaming on after connect.
static volatile bool streaming = true;

// Button callback (called from the simple_button driver on press/release).
void sl_button_on_change(const sl_button_t *handle)
{
  if (sl_button_get_state(handle) != SL_SIMPLE_BUTTON_PRESSED) {
    return;
  }
  if (handle == &sl_button_btn0) {
    streaming = true;    // START
  } else if (handle == &sl_button_btn1) {
    streaming = false;   // STOP
  }
}

// Application Init.
void app_init(void)
{
  // 1. Keep high-frequency clocks alive (prevents EM2 deep sleep from killing UART)
  sl_power_manager_add_em_requirement(SL_POWER_MANAGER_EM1);

  // 2. Enable VCOM bridge
  sl_board_enable_vcom();

  // 3. Turn LED0 ON
  sl_led_turn_on(&sl_led_led0);

  // 4. Power IMU rail
  sl_board_enable_sensor(SL_BOARD_SENSOR_IMU);

  // 5. Test if sl_imu_init hangs:
  // If the board hangs inside this function, LED0 will stay permanently ON.
  sl_status_t imu_status = sl_imu_init();
  if (imu_status == SL_STATUS_OK) {
    // Required: moves IMU state from INITIALIZING to READY.
    // Without this, sl_imu_is_data_ready() always returns false.
    sl_imu_configure(100.0f);
  }
  printf("sl_imu_init status = 0x%lx, imu state = %d\r\n",
         (unsigned long)imu_status, sl_imu_get_state());

  // If it survives sl_imu_init(), turn LED0 OFF as proof
  sl_led_turn_off(&sl_led_led0);
}

// Application Process Action.
void app_process_action(void)
{
  // Wakes up on advertising tick (~100ms) or loop.
  // 10 ticks = 1 second.
  if (++loop_counter >= 10) {
    loop_counter = 0;
    const char test_msg[] = "UART_OK\r\n";
    sl_status_t write_status = sl_iostream_write(sl_iostream_vcom_handle, test_msg, sizeof(test_msg) - 1);

    if (write_status == SL_STATUS_OK) {
      // Transmission accepted by driver: toggle LED to indicate OK
      sl_led_toggle(&sl_led_led0);
      printf("AI PEN ALIVE: IMU ready = %d\r\n", sl_imu_is_data_ready());
    } else {
      // Transmission failed (e.g. SL_STATUS_INVALID_STATE or SL_STATUS_NOT_INITIALIZED)
      // Solid ON indicates write failure
      sl_led_turn_on(&sl_led_led0);
    }
    // app_log("Tick! IMU data ready: %d\r\n", sl_imu_is_data_ready());
  }

  if (sl_imu_is_data_ready()) {
    int16_t acc[3];
    int16_t gyro[3];

    sl_imu_update();
    sl_imu_get_acceleration(acc);   // mg
    sl_imu_get_gyro(gyro);          // 0.01 dps

    if (notify_enabled && streaming) {
      // Pack 6 x int16 little-endian (Cortex-M is little-endian).
      int16_t payload[6] = { acc[0], acc[1], acc[2], gyro[0], gyro[1], gyro[2] };
      // Ignore errors such as NO_MORE_RESOURCE: just drop the sample.
      sl_bt_gatt_server_notify_all(gattdb_imu_data, sizeof(payload), (uint8_t *)payload);
    }
    // Per-sample UART printing removed: it blocks the loop and slows BLE.
  }
}

/**************************************************************************//**
 * Bluetooth stack event handler.
 * This overrides the default weak implementation.
 *
 * @param[in] evt Event coming from the Bluetooth stack.
 *****************************************************************************/
void sl_bt_on_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;

  switch (SL_BT_MSG_ID(evt->header)) {
    // -------------------------------
    // This event indicates the device has started and the radio is ready.
    // Do not call any stack command before receiving this boot event!
    case sl_bt_evt_system_boot_id:
      // Create an advertising set.
      sc = sl_bt_advertiser_create_set(&advertising_set_handle);
      app_assert_status(sc);

      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Set advertising interval to 100ms.
      sc = sl_bt_advertiser_set_timing(
        advertising_set_handle,
        160, // min. adv. interval (milliseconds * 1.6)
        160, // max. adv. interval (milliseconds * 1.6)
        0,   // adv. duration
        0);  // max. num. adv. events
      app_assert_status(sc);
      // Start advertising and enable connections.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_legacy_advertiser_connectable);
      app_assert_status(sc);
      break;

    // -------------------------------
    // This event indicates that a new connection was opened.
    case sl_bt_evt_connection_opened_id:
      break;

    // -------------------------------
    // This event indicates that a connection was closed.
    case sl_bt_evt_gatt_server_characteristic_status_id:
      if (evt->data.evt_gatt_server_characteristic_status.characteristic == gattdb_imu_data
          && evt->data.evt_gatt_server_characteristic_status.status_flags == sl_bt_gatt_server_client_config) {
        notify_enabled = (evt->data.evt_gatt_server_characteristic_status.client_config_flags
                          & sl_bt_gatt_notification) != 0;
        printf("BLE notify %s\r\n", notify_enabled ? "ON" : "OFF");
      }
      break;

    case sl_bt_evt_connection_closed_id:
      notify_enabled = false;
      // Generate data for advertising
      sc = sl_bt_legacy_advertiser_generate_data(advertising_set_handle,
                                                 sl_bt_advertiser_general_discoverable);
      app_assert_status(sc);

      // Restart advertising after client has disconnected.
      sc = sl_bt_legacy_advertiser_start(advertising_set_handle,
                                         sl_bt_legacy_advertiser_connectable);
      app_assert_status(sc);
      break;

    ///////////////////////////////////////////////////////////////////////////
    // Add additional event handlers here as your application requires!      //
    ///////////////////////////////////////////////////////////////////////////

    // -------------------------------
    // Default event handler.
    default:
      break;
  }
}
