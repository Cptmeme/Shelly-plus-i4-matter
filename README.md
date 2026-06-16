# Shelly Plus i4 — Matter-over-Wi-Fi firmware (v1.0.0)

Reports to Matter as **Manufacturer: Shelly**, **Model: Shelly Plus i4**,
**Firmware: 1.0.0**, **Hardware: 2.0.0**.

Custom firmware that turns a **Shelly Plus i4** (ESP32-U4WDH) into a **Matter**
scene controller over Wi-Fi. Each of the 4 inputs is exposed as a Matter
**Generic Switch** with **Single / Double / Long press**, so you can bind them
to scenes/automations in Apple Home, Google Home, Home Assistant, SmartThings,
etc.

## What you get

- **4 stateless buttons** (i4 inputs I1–I4), each reporting Single / Double /
  Long press to your Matter controller.
- **Status LED** reflects state (BLE advertising / connecting / connected).
- **Factory reset**: hold the **back button (in the case) for ~5 s** → erases the
  Matter pairing + Wi-Fi credentials and reboots into pairing mode.
  - NOTE: after a reset, **power-cycle the device once** if commissioning stalls
    at the first step (known ESP32 BLE-after-soft-reboot quirk).

| i4 input | ESP32 GPIO | Matter endpoint |
|----------|-----------|-----------------|
| I1 | GPIO12 | 1 |
| I2 | GPIO14 | 2 |
| I3 | GPIO27 | 3 |
| I4 | GPIO26 | 4 |

## Files

- `shelly_plus_i4_matter_v1.0.0.bin` — **full 4 MB image, flash at offset `0x0`** (recommended).
- `shelly_plus_i4_matter_v1.0.0_app_0x20000.bin` — app only (flash at `0x20000`, for updates that keep the same bootloader/partition table).

SHA-256 (full image): `906f5be7aaf215b50f5bb056e3f21ec3926a8b4c29013d3a70d7497908beb0dc`

## ⚠️ Before you flash

1. **Back up the stock firmware first** so you can restore your i4 later:
   ```
   esptool --port <PORT> --baud 115200 read_flash 0x0 0x400000 shelly_i4_stock_backup.bin
   ```
2. This **overwrites the original Shelly firmware** — the device will no longer
   work with the Shelly app/cloud until you restore the backup.
3. The Shelly Plus i4 has **no USB**. You must wire a 3.3 V USB-UART adapter to
   the programming pads: **3V3, GND, TX→RX, RX→TX**, and **GPIO0** for download
   mode. **Power the board from the adapter's 3.3 V only — NEVER connect mains
   while the UART is attached.**
4. Use a 3.3 V source that can supply **≥ 500 mA** (the Wi-Fi/BLE radio spikes
   during first-boot RF calibration). A weak supply browns out during pairing.

## Enter download mode (these pads usually have no auto-reset)

1. Connect **GPIO0 → GND** and keep it held.
2. While holding GPIO0, power-cycle the board (or pulse EN/RST → GND).
3. The chip is now in the ROM bootloader. Keep GPIO0 held during flashing.

## Flash

Full image (single command):
```
esptool --chip esp32 --port <PORT> --baud 115200 \
  write_flash 0x0 shelly_plus_i4_matter_v1.2.0.bin
```
Then **release GPIO0 and power-cycle** to boot.

Or use a browser flasher (ESP Web Tools / esptool-js) and write the full image
at offset `0x0`.

## Pair it

On first boot it advertises over BLE for commissioning. Add it in your Matter
app. Because this build uses **Matter test credentials** (test Vendor/Product
IDs + test DAC), your controller will show an **"uncertified accessory"**
warning — accept it to continue. (Production certification requires a
CSA-issued Vendor ID + device attestation certificate.)

## Compatibility notes

- Built for the **ESP32-U4WDH** in the Shelly Plus i4. Do not flash to other
  Shelly models — the GPIO map and embedded-flash pinout are i4-specific.
- Requires a unit **without flash encryption / secure boot** (standard for
  retail i4 units). A locked unit will reject a plaintext image.
