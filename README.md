# Tab5: update the firmware of the ESP32-C6 over SDIO

A standalone ESP-IDF application that replaces the firmware of the ESP32-C6 co-processor of the
[M5Stack Tab5](https://docs.m5stack.com/en/core/Tab5) through the SDIO link it shares with the ESP32-P4, with no soldering
and no extra hardware. Wi-Fi and Bluetooth on the Tab5 go through the C6, and with ESP-Hosted 3.x on the P4 the C6 needs a matching firmware.
This application is independent of whatever firmware you run on the P4 afterwards.

It is a fork and rework for the Tab5 of [crowpanel-p4-c6-sdio-ota](https://github.com/lboshuizen/crowpanel-p4-c6-sdio-ota) by
lboshuizen, which does the same for the Elecrow CrowPanel 7" (see `NOTICE`).

**Status: it builds in CI with ESP-IDF 6.1 (see `.github/workflows/build.yml`). It has not been run on a Tab5.**

## Workflow: update the C6, then flash your firmware

This is its own firmware image. It updates the core firmware of the device (the C6) and nothing else. The order is:

1. Flash **this application** to the Tab5 (P4) by itself: use the [prebuilt files](#prebuilt-files-no-build-needed), or build it yourself.
2. Let it run until the log says the C6 reports the new version (`[PASS]`). The C6 keeps that firmware in its own flash.
3. **Manually flash your own firmware** over it. This application is then gone from the P4 and
   isn't needed again unless the C6 firmware has to change.

Nothing here flashes or bundles your P4 firmware: that step is yours.

## Prebuilt files, no build needed

Each [release](https://github.com/shadowlab/tab5-p4-c6-sdio-ota/releases) has the updater already built, with the ESP-Hosted
v3.0.9 C6 firmware embedded, so there is nothing to download or compile besides [esptool](https://docs.espressif.com/projects/esptool/):

```bash
pip install esptool
esptool --chip esp32p4 --baud 460800 --port <serial port> write_flash 0x0 tab5_c6_ota_merged.bin
```

Then follow the serial log (`idf.py monitor`, or any serial terminal at 115200 baud) until it prints `[PASS]`, and flash your own firmware.

| File | What it is |
|---|---|
| `tab5_c6_ota_merged.bin` | The updater with bootloader and partition table in one file, written at 0x0 |
| `tab5_c6_ota.bin`, `bootloader.bin`, `partition-table.bin` | The same as separate parts, offsets in `flash.txt` |
| `network_adapter_esp32c6_v3.0.9.bin` | The ESP32-C6 firmware that gets sent (ESP-Hosted v3.0.9, ESPHome's build, unmodified). Already inside the files above, here for those who build the updater themselves: copy it to `firmware/network_adapter.bin` |
| `flash.txt`, `SHA256SUMS` | Flash commands and checksums (`sha256sum -c SHA256SUMS`) |

The first release has not been run on a Tab5. The P4 revision in the prebuilt files is "older than v3" (see [Build and flash](#build-and-flash)):
for a v3 chip, build it yourself with the settings changed.

## Why

The Tab5 ships with a C6 running ESP-Hosted slave firmware 1.4.1 (`ESP32C6-WiFi-SDIO-Interface-V1.4.1`, the file in
M5Stack's `M5Tab5-UserDemo`, which pairs it with ESP-Hosted host 1.4.0). This application uses ESP-Hosted host
3.0.x, and the host and the co-processor are expected to run matching versions. The C6 has no USB port and no
UART that the P4 or an external connector can reach, so the easy way to change its firmware is the SDIO link, which is what this does
(the C6's internal download interface is the fallback, see below).

The same image also decides what the C6 can do: Bluetooth needs a firmware built with the BT controller enabled, see below.

## How it works

1. The C6 is powered. Its supply is switched by an IO expander (PI4IOE5V6408 at 0x44 on the I2C bus, SDA GPIO31, SCL
   GPIO32), so a bare ESP-Hosted application would never see it.
2. ESP-Hosted is started with the pins of the Tab5 (SDMMC slot 1, CLK 12, CMD 13, D0-D3 11-8, C6 reset on GPIO15, 4-bit at
   20 MHz) and connects to the C6. Wi-Fi is never started.
3. The embedded image (`firmware/network_adapter.bin`) is checked (ESP image, chip id 13 = ESP32-C6, fits the 0x180000
   slot, project and version are logged) and the version of the C6 is read. If it already is the version of the host the
   application stops, unless `TAB5_C6_OTA_FORCE` is set.
4. The image is sent with ESP-Hosted's OTA calls (begin, write in 1500 byte chunks, end, activate), the C6 writes it to
   its **inactive** OTA slot and boots it after activation. The running firmware is not touched until then.
5. After a few seconds the version of the C6 is read again and compared with the host's.

The log marks what happens with `[PHASE]`, `[PASS]`, `[FAIL]`, `[WARN]` and `[DIAG]`.

The OTA slots of the factory firmware: its partition table has two app slots of 0x180000 (1.5 MB) at 0x10000 and
0x190000, taken from the image in M5Stack's repository. An image bigger than that is refused.

## Build and flash

Only needed if you don't use the [prebuilt files](#prebuilt-files-no-build-needed). The build embeds the C6 firmware, so first save
`network_adapter_esp32c6_v3.0.9.bin` from the [latest release](https://github.com/shadowlab/tab5-p4-c6-sdio-ota/releases) as
`firmware/network_adapter.bin`.

```bash
. $IDF_PATH/export.sh            # ESP-IDF 6.1
idf.py set-target esp32p4
idf.py build                     # fails with a message if firmware/network_adapter.bin is missing
idf.py -p <serial port> flash monitor
```

This replaces the firmware of the P4 (flash your normal firmware over it afterwards). The C6 keeps its new
firmware, it has its own flash. Running the application again is safe: it reports that the C6 already is at the version
of the host.

The sdkconfig selects a P4 **older than revision v3**, like the Tab5 (the two kinds of revisions are mutually exclusive
in ESP-IDF 6.x, a bootloader for one doesn't boot on the other). Change `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` and
`CONFIG_ESP32P4_REV_MIN_1` in `sdkconfig.defaults` if your chip is v3.

Settings (`idf.py menuconfig`, "Tab5 C6 OTA"): chunk size, the largest image accepted, forcing the transfer, the time to wait
for the reboot. The SDIO clock is `CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ` in `sdkconfig.defaults` (20000), try 10000 if the
transfer fails.

## Partition layouts

**P4 (`partitions.csv`).** The C6 image is embedded in the application, so
there is no `otadata` and no LittleFS partition, unlike the CrowPanel original:

```
Offset     Name       Type/Subtype   Size
0x009000   nvs        data/nvs       16 KB
0x00F000   phy_init   data/phy       4 KB
0x010000   factory    app/factory    3 MB    this application, embeds the C6 image
```

`idf.py flash` also writes the bootloader and the partition table (at 0x8000). Flashing your own firmware afterwards writes
its own partition table, follow its instructions for that step.

**C6 (set by the factory firmware, not changed by an OTA).** Two app slots, taken from the image in M5Stack's repository:

```
Offset     Name    Size
0x010000   ota_0   0x180000 (1.5 MB)
0x190000   ota_1   0x180000 (1.5 MB)
```

The OTA writes the inactive slot, and an image over `TAB5_C6_OTA_MAX_IMAGE_SIZE` (0x180000) is refused before anything is sent.

## Things that can go wrong

- **No answer from the C6.** This host speaks ESP-Hosted 3.x and the factory firmware is 1.4.1. If the two don't understand each other
  the transport doesn't come up and nothing can be sent over it. This is the main thing that has not been tried. The fallback is
  the C6's UART, see [If updating fails](#if-updating-fails-the-c6-download-interface).
- **Old firmware without the separate activate call.** Firmware older than 2.6 switches to the new image when the transfer
  ends. The activate call then fails, which the application logs as a warning and verifies by reading the version.
- **A bad image.** The C6 has no rollback: an image that passes the checks but crashes leaves it in a boot loop, and only
  its download mode brings it back, see [If updating fails](#if-updating-fails-the-c6-download-interface).
- **The transfer stops half way** ([FAIL] OTA write): the current firmware is untouched. Lower the clock or the chunk size.
- Only the Tab5 was considered. The pin and power set-up in `main/tab5_power.c` and `sdkconfig.defaults` is its.

## If updating fails: the C6 download interface

The Tab5 PCB has a reserved, internal download interface (UART pads) for the ESP32-C6. It is the way back in when the SDIO update
can't work or has left the C6 in a boot loop, because the ROM download mode of the ESP32-C6 doesn't depend on the firmware that is on it.

M5Stack documents the procedure in
[Tab5 ESP32-C6 Wi-Fi Module Restore Factory Firmware](https://docs.m5stack.com/en/guide/tab5/c6_wifi_restore):

1. Get M5Stack's **M5Burner** and download the Tab5 Wi-Fi (SDIO) factory firmware in it.
2. Connect a USB-TTL converter to the download interface: a row of six pads next to the C6 module, beside the Wi-Fi symbol printed on
   the board. M5Stack uses its official [ESP32 Downloader](https://shop.m5stack.com/products/esp32-downloader-kit), whose six pins are in
   the same order as the pads and plug straight onto them, nothing to wire (its photo is on M5Stack's page). The silkscreen of the
   pads, in order:

   | Pad | Meaning |
   |---|---|
   | GND | ground |
   | G9 | C6 GPIO9, the boot (strapping) pin |
   | RST | C6 reset |
   | RXD | C6 UART receive |
   | TXD | C6 UART transmit |
   | 3V3 | 3.3 V |

   Without the M5Stack Downloader, wire another USB-TTL converter by these names: the adapter's TX goes to the pad `RXD` and its RX to
   the pad `TXD` (the pad names are the C6's, so the lines cross).
   Whether the pads can also power the C6 on their own, and how the board is powered during the flash, is not stated by M5Stack; follow
   their page.
3. With the device in download mode, pick the serial port in M5Burner and click **Burn**.

That restores M5Stack's **factory** C6 firmware, not ESP-Hosted 3.0.9, so afterwards run this updater again. Flashing the 3.0.9 image
straight over the UART is not covered by M5Stack's page and has not been tried here. Follow M5Stack's page for the details (how the
device is put into download mode, voltages) and check your board before connecting anything.

## Project structure

```
├── CMakeLists.txt            Root build definition (project tab5_c6_ota)
├── sdkconfig.defaults        Tab5 P4 settings, ESP-Hosted SDIO board preset, OTA settings
├── partitions.csv            P4 flash layout (nvs, phy_init, factory 3M)
├── HARDWARE.md               Tab5 C6 power, SDIO pins, C6 flash layout
├── firmware/                 Put network_adapter.bin here (not committed)
└── main/
    ├── main.c                OTA workflow (power and connect, version, transfer, verify)
    ├── tab5_power.c/.h       Powers the C6 through the IO expander
    ├── idf_component.yml     esp_hosted ^3.0.9
    └── Kconfig.projbuild     "Tab5 C6 OTA" settings
```

## License

Apache-2.0 (`LICENSE`, `NOTICE`).
