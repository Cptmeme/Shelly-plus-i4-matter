# Shelly Plus i4 — Matter-over-Wi-Fi firmware (v1.0.0)

Reports to Matter as **Manufacturer: Shelly**, **Model: Shelly Plus i4**,
**Firmware: 1.0.0**, **Hardware: 2.0.0**.

Custom firmware that turns a **Shelly Plus i4** (ESP32-U4WDH) into a **Matter**
scene controller over Wi-Fi. Each of the 4 inputs is exposed as a Matter
**Generic Switch** with **Single / Double / Long press**, so you can bind them
to scenes/automations in Apple Home, Google Home, Home Assistant, SmartThings,
etc.

> **⚠️ Disclaimer.** Flashing third-party firmware modifies your device and may
> void your warranty. Incorrect flashing — or connecting mains while the UART is
> attached — can destroy the device, your computer's USB port, or injure you.
> Always back up the stock firmware first. You assume all responsibility for any
> damage, data loss, or device failure. This project is not affiliated with
> Shelly, Allterco Robotics, CSA, or Espressif Systems.

> **This is the prebuilt-binary guide.** It walks you through flashing the
> ready-made `.bin` files in the [`prebuilt/`](prebuilt/) folder — no toolchain
> or compiler needed. If you want to build from source instead, the ESP-IDF /
> esp-matter build flow is documented in the base repo's
> [build guide](https://github.com/automatous-io/shelly-1-gen4-matter-thread/blob/main/docs/BUILDING.md)
> (written for the Shelly 1 Gen4, but the toolchain and steps are the same — see
> [Credits](#credits)).

---

## Contents

- [What you get](#what-you-get)
- [Input → GPIO → endpoint map](#input--gpio--endpoint-map)
- [Prebuilt files](#prebuilt-files)
- [What you'll need](#what-youll-need)
- [⚠️ Safety — read before you wire anything](#️-safety--read-before-you-wire-anything)
- [Wiring the USB-UART adapter](#wiring-the-usb-uart-adapter)
- [Step 1 — Enter download mode](#step-1--enter-download-mode)
- [Step 2 — Back up the stock firmware](#step-2--back-up-the-stock-firmware)
- [Step 3 — Flash the Matter firmware](#step-3--flash-the-matter-firmware)
- [Step 4 — Boot and verify](#step-4--boot-and-verify)
- [Pair it (commissioning)](#pair-it-commissioning)
- [Factory reset](#factory-reset)
- [Restore the stock firmware](#restore-the-stock-firmware)
- [Compatibility notes](#compatibility-notes)
- [Credits](#credits)
- [License](#license)

---

## What you get

- **4 stateless buttons** (i4 inputs I1–I4), each reporting Single / Double /
  Long press to your Matter controller.
- **Status LED** reflects state (BLE advertising / connecting / connected).
- **Factory reset**: hold the **back button (in the case) for ~5 s** → erases the
  Matter pairing + Wi-Fi credentials and reboots into pairing mode.
  - NOTE: after a reset, **power-cycle the device once** if commissioning stalls
    at the first step (known ESP32 BLE-after-soft-reboot quirk).

---

## Input → GPIO → endpoint map

| i4 input | ESP32 GPIO | Matter endpoint |
|----------|-----------|-----------------|
| I1 | GPIO12 | 1 |
| I2 | GPIO14 | 2 |
| I3 | GPIO27 | 3 |
| I4 | GPIO26 | 4 |

---

## Prebuilt files

These live in the [`prebuilt/`](prebuilt/) folder (the commands below assume you
run them from the repo root, so they're prefixed with `prebuilt/`):

| File | Flash offset | Size | When to use |
|------|--------------|------|-------------|
| `prebuilt/shelly_plus_i4_matter_v1.0.0.bin` | `0x0` | 4 MB (4,194,304 B) | **Recommended.** Full image — bootloader + partition table + app in one write. Use this for a first install. |
| `prebuilt/shelly_plus_i4_matter_v1.0.0_app_0x20000.bin` | `0x20000` | ~1.41 MB (1,483,056 B) | App only. Use for updates that keep the **same** bootloader/partition table. Skip this unless you know you need it. |

**SHA-256 — verify before flashing:**

```
shelly_plus_i4_matter_v1.0.0.bin            906f5be7aaf215b50f5bb056e3f21ec3926a8b4c29013d3a70d7497908beb0dc
shelly_plus_i4_matter_v1.0.0_app_0x20000.bin 25f0784e501d53034ddd9c903f9c0843b19fb7ba9bc570d70a27e77df121f693
```

Check on your machine before flashing (run from the repo root):

```bash
# macOS / Linux
shasum -a 256 prebuilt/shelly_plus_i4_matter_v1.0.0.bin

# Windows (PowerShell)
Get-FileHash prebuilt\shelly_plus_i4_matter_v1.0.0.bin -Algorithm SHA256
```

The output must match the hash above exactly. If it doesn't, re-download — do
**not** flash a mismatched file.

---

## What you'll need

- A **Shelly Plus i4** (the AC-powered ESP32-U4WDH model — **not** the i4DC).
- A **3.3 V USB-UART adapter** (CP2102, CH340, FT232 in 3.3 V mode, etc.). It
  **must** be able to supply **≥ 500 mA** on 3.3 V — the Wi-Fi/BLE radio spikes
  during first-boot RF calibration and a weak adapter will brown out mid-pairing.
- A few **jumper wires** and something to hold **GPIO0 → GND** (the i4 pads
  usually have no auto-reset circuit, so you bridge boot mode by hand).
- **[esptool](https://github.com/espressif/esptool)** (`pip install esptool`) for
  the command-line path, **or** a Chromium-based browser (Chrome/Edge) for the
  browser-flasher path — no install needed.
- The `.bin` files from the [`prebuilt/`](prebuilt/) folder.

---

## ⚠️ Safety — read before you wire anything

> **NEVER connect mains (line voltage) to the i4 while the USB-UART is attached.**
> The i4's low-voltage side is **not** galvanically isolated from its mains side.
> Powering it from both at once can cause:
> - personal electrocution risk,
> - permanent destruction of the i4,
> - permanent destruction of your computer's USB port (or the whole computer).

- Flash the i4 **bench-side only**, powered by the adapter's **3.3 V** line and
  nothing else. Remove it from any wall box / fixture first.
- The programming pads are **3.3 V only**. Do **not** connect a 5 V pin — many
  USB-UART boards expose both; verify you're on 3.3 V before powering up.
- This **overwrites the original Shelly firmware**. The device will no longer
  work with the Shelly app/cloud until you [restore the backup](#restore-the-stock-firmware).
  **Back up first** (Step 2) — it is the only way back to factory.

---

## Wiring the USB-UART adapter

The Shelly Plus i4 has **no USB**. You wire the adapter to the programming pads
on the board. Note the **TX↔RX crossover** — adapter TX goes to the board's RX,
and adapter RX goes to the board's TX.

| USB-UART adapter | Shelly Plus i4 pad |
|------------------|--------------------|
| 3V3 (3.3 V)      | 3V3                |
| GND              | GND                |
| TXD              | RX                 |
| RXD              | TX                 |
| (jumper to GND)  | GPIO0 (boot)       |

> If you wire adapter-TX → board-TX and adapter-RX → board-RX (no crossover), the
> chip will not respond. The crossover above is correct.

> **Do not connect the adapter's 5 V pin.** 3.3 V only.

---

## Step 1 — Enter download mode

The i4 pads typically have no auto-reset, so you put the ESP32 into the ROM
bootloader by hand:

1. With the 3.3 V line **disconnected**, bridge **GPIO0 → GND** and keep it held.
2. While still holding GPIO0 low, **connect the 3.3 V line** (or pulse EN/RST → GND)
   to power the board. It boots into the ROM bootloader.
3. Keep GPIO0 held to GND until the flashing tool has connected. Once the
   bootloader is talking, you can release it.

Confirm the chip is detected before continuing:

```bash
esptool --chip esp32 --port <PORT> --baud 115200 chip_id
```

Replace `<PORT>` with `/dev/cu.usbserial-XXXX` (macOS), `/dev/ttyUSB0` (Linux),
or `COM3` (Windows). If this fails with "Failed to connect", the board isn't in
download mode — recheck the GPIO0 → GND bridge and re-power.

---

## Step 2 — Back up the stock firmware

**Do not skip this.** The backup is the only way to return the i4 to its factory
Shelly firmware later.

```bash
esptool --chip esp32 --port <PORT> --baud 115200 \
  read_flash 0x0 0x400000 shelly_i4_stock_backup.bin
```

This reads the full **4 MB** flash. When it finishes, **verify the file is exactly
4,194,304 bytes** — a smaller file means the read was truncated; redo it before
going further. Store the backup somewhere safe; if you're flashing several i4s,
keep each one's backup separately labeled.

---

## Step 3 — Flash the Matter firmware

Put the board in [download mode](#step-1--enter-download-mode) first. Use **one**
of the two paths below.

### Option A — esptool (command line)

Full image (recommended, single command — run from the repo root):

```bash
esptool --chip esp32 --port <PORT> --baud 115200 \
  write_flash 0x0 prebuilt/shelly_plus_i4_matter_v1.0.0.bin
```

App-only update (only if you're keeping the existing bootloader/partition table):

```bash
esptool --chip esp32 --port <PORT> --baud 115200 \
  write_flash 0x20000 prebuilt/shelly_plus_i4_matter_v1.0.0_app_0x20000.bin
```

### Option B — browser flasher (no install)

Use a Web Serial flasher such as
[ESP Web Tools](https://esphome.github.io/esp-web-tools/) /
[esptool-js](https://espressif.github.io/esptool-js/) in Chrome or Edge:

1. Click **Connect** and pick your USB-UART serial port.
2. Select the full image `shelly_plus_i4_matter_v1.0.0.bin` (from the `prebuilt/`
   folder) and set the offset to `0x0`.
3. Flash and wait for the success message.

---

## Step 4 — Boot and verify

1. **Release GPIO0** (remove the GPIO0 → GND bridge). If GPIO0 is still grounded
   at power-up, the chip boots back into the bootloader instead of your firmware.
2. **Power-cycle**: disconnect 3.3 V, wait ~2 s, reconnect.
3. Watch the **status LED** — once it's advertising over BLE for commissioning,
   the firmware is running. Proceed to [Pair it](#pair-it-commissioning).

For boot logs / troubleshooting, open a serial monitor at **115200** baud
(`screen /dev/cu.usbserial-XXXX 115200` on macOS/Linux, PuTTY on Windows). The
3.3 V line can stay connected for power and monitoring — just keep GPIO0 free.

---

## Pair it (commissioning)

On first boot the device advertises over BLE for Matter commissioning. Add it in
your Matter app (Apple Home, Google Home, Home Assistant, SmartThings, …) using
its setup code / QR.

Because this build uses **Matter test credentials** (test Vendor/Product IDs +
test DAC), your controller will show an **"uncertified accessory"** warning —
accept it to continue. (Production certification requires a CSA-issued Vendor ID
+ device attestation certificate.)

Once paired, each of the 4 inputs appears as a stateless **Generic Switch** you
can bind to scenes and automations.

---

## Factory reset

Hold the **back button (inside the case) for ~5 s**. This erases the Matter
pairing and Wi-Fi credentials and reboots into pairing mode (LED returns to the
BLE-advertising pattern). Remove the device from your Matter ecosystems before
re-commissioning.

> If commissioning stalls at the very first step after a reset, **power-cycle the
> device once** — a known ESP32 BLE-after-soft-reboot quirk.

---

## Restore the stock firmware

To go back to factory Shelly firmware, flash the backup you made in
[Step 2](#step-2--back-up-the-stock-firmware):

1. Put the i4 back into [download mode](#step-1--enter-download-mode).
2. Write the backup at offset `0x0`:

   ```bash
   esptool --chip esp32 --port <PORT> --baud 115200 \
     write_flash 0x0 shelly_i4_stock_backup.bin
   ```
3. Release GPIO0 and power-cycle. The i4 boots the restored stock firmware and
   behaves as a factory unit again.

---

## Compatibility notes

- Built for the **ESP32-U4WDH** in the Shelly Plus i4. Do not flash to other
  Shelly models — the GPIO map and embedded-flash pinout are i4-specific.
- Requires a unit **without flash encryption / secure boot** (standard for
  retail i4 units). A locked unit will reject a plaintext image.

---

## Credits

The base of this code comes from
[**automatous-io/shelly-1-gen4-matter-thread**](https://github.com/automatous-io/shelly-1-gen4-matter-thread)
— a Matter firmware project for the Shelly 1 Gen4. This Shelly Plus i4 build
adapts that work to the i4's ESP32-U4WDH hardware and exposes the four inputs as
Matter Generic Switches over Wi-Fi (the i4's ESP32 has no 802.15.4 radio, so it
runs Matter over Wi-Fi rather than over Thread). Thanks to that project for the
foundation. Both the base project and Espressif's
[esp-matter](https://github.com/espressif/esp-matter) SDK are licensed under the
Apache License 2.0.

---

## License

This firmware is released under the **GNU General Public License v3.0** — see
[LICENSE](LICENSE).
