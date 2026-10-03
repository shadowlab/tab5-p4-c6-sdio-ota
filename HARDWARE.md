# M5Stack Tab5: ESP32-C6 hardware notes

What this application relies on. All values are taken from `main/tab5_power.c`, `sdkconfig.defaults` and the factory C6 image
as documented for the Tab5; none of it has been re-checked against a schematic here.

## C6 power

The supply of the ESP32-C6 is switched by an IO expander, so ESP-Hosted alone never sees the C6.

| Item | Value |
|------|-------|
| Expander | PI4IOE5V6408 (the second one on the board), I2C address 0x44 |
| I2C bus | SDA GPIO31, SCL GPIO32, 400 kHz |
| C6 supply | expander output bit 0 (`WLAN_PWR_EN`), set high |

`tab5_power_on_c6()` resets the expander, configures it like M5Stack's BSP (`bsp_io_expander_pi4ioe_init`) and sets only the C6
supply bit. USB 5V and charging outputs stay off.

## SDIO link (P4 to C6)

Configured through ESP-Hosted's `CONFIG_ESP32P4_TAB5_C6_BOARD` preset.

| Signal | P4 GPIO |
|--------|---------|
| SDMMC slot | 1 |
| CLK | 12 |
| CMD | 13 |
| D0 to D3 | 11 to 8 |
| C6 reset | 15 |

4-bit bus, 20 MHz here (`CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ`). Try 10 MHz if the transfer fails.

## C6 flash

| Name | Offset | Size |
|------|--------|------|
| ota_0 | 0x10000 | 0x180000 |
| ota_1 | 0x190000 | 0x180000 |

Two app slots, no factory partition. The OTA writes the inactive slot. There is no rollback on the C6: an image that boots and
then crashes leaves it in a boot loop, recoverable only through the C6 download mode over the internal UART pads (a row of six pads beside the C6 module: GND, G9, RST, RXD, TXD, 3V3, see the README, "If updating fails").

Factory firmware: ESP-Hosted slave 1.4.1 (`ESP32C6-WiFi-SDIO-Interface-V1.4.1`), the one in M5Stack's `M5Tab5-UserDemo`.

## P4

Older than revision v3 (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`), 16 MB flash, QIO, 360 MHz.
