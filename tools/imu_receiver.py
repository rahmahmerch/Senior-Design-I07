"""Receive IMU_Pen BLE notifications, log to CSV, and plot in real time.

Setup:   pip install bleak matplotlib
Run:     python3 tools/imu_receiver.py
macOS:   allow Bluetooth for your terminal/VS Code in System Settings > Privacy.
"""
import asyncio
import csv
import struct
import threading
import time
from collections import deque
from datetime import datetime

import matplotlib.pyplot as plt
from matplotlib.animation import FuncAnimation
from bleak import BleakClient, BleakScanner

DEVICE_NAME = "IMU_Pen"
IMU_CHAR_UUID = "6e1f0002-b5a3-f393-e0a9-e50e24dcca9e"
WINDOW = 500  # samples shown in the plot
STALL_S = 0.3  # no packet for this long => treat stream as stopped
last_rx = [0.0]

t0 = time.time()
buf = {k: deque(maxlen=WINDOW) for k in ("t", "ax", "ay", "az", "gx", "gy", "gz")}
lock = threading.Lock()
csv_name = f"imu_log_{datetime.now():%Y%m%d_%H%M%S}.csv"
csv_file = open(csv_name, "w", newline="")
writer = csv.writer(csv_file)
writer.writerow(["time_s", "ax_mg", "ay_mg", "az_mg", "gx_dps", "gy_dps", "gz_dps"])
count = 0


def on_notify(_, data: bytearray):
    global count
    if len(data) != 12:
        return
    ax, ay, az, gx, gy, gz = struct.unpack("<6h", data)
    gx, gy, gz = gx / 100.0, gy / 100.0, gz / 100.0  # 0.01 dps -> dps
    t = time.time() - t0
    writer.writerow([f"{t:.3f}", ax, ay, az, gx, gy, gz])
    with lock:
        last_rx[0] = time.time()
        for k, v in zip(buf, (t, ax, ay, az, gx, gy, gz)):
            buf[k].append(v)
    count += 1
    if count % 100 == 0:
        csv_file.flush()


async def ble_main():
    print(f"Scanning for '{DEVICE_NAME}'...")
    dev = await BleakScanner.find_device_by_name(DEVICE_NAME, timeout=15)
    if dev is None:
        print("Device not found. Is it advertising?")
        return
    async with BleakClient(dev) as client:
        print("Connected. Logging to", csv_name)
        await client.start_notify(IMU_CHAR_UUID, on_notify)
        while client.is_connected:
            await asyncio.sleep(0.5)
    print("Disconnected.")


threading.Thread(target=lambda: asyncio.run(ble_main()), daemon=True).start()

fig, (ax_a, ax_g) = plt.subplots(2, 1, sharex=True)
lines = {}
for axis, keys, title in ((ax_a, ("ax", "ay", "az"), "Accelerometer (mg)"),
                          (ax_g, ("gx", "gy", "gz"), "Gyroscope (dps)")):
    axis.set_title(title)
    for k in keys:
        (lines[k],) = axis.plot([], [], label=k)
    axis.legend(loc="upper right")
ax_g.set_xlabel("time (s)")


def update(_):
    with lock:
        # Stream stopped (BTN1): no packets arrive, so extend the plot with the
        # last value to show a flat line. These filler points are NOT logged to CSV.
        if buf["t"] and time.time() - last_rx[0] > STALL_S:
            now = time.time() - t0
            buf["t"].append(now)
            for k in ("ax", "ay", "az", "gx", "gy", "gz"):
                buf[k].append(buf[k][-1])
        t = list(buf["t"])
        for k, ln in lines.items():
            ln.set_data(t, list(buf[k]))
    if t:
        for axis in (ax_a, ax_g):
            axis.set_xlim(t[0], max(t[-1], t[0] + 1))
            axis.relim()
            axis.autoscale_view(scalex=False)
    return list(lines.values())


ani = FuncAnimation(fig, update, interval=100, cache_frame_data=False)
try:
    plt.show()
finally:
    csv_file.close()
