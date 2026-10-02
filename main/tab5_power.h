/* SPDX-License-Identifier: Apache-2.0 */
#pragma once

#include "esp_err.h"

/* Powers the ESP32-C6 of the M5Stack Tab5. Its supply is switched by the second IO expander (a PI4IOE5V6408 on the
 * I2C bus, SDA GPIO31 and SCL GPIO32), the P4 can't reach it otherwise. The sequence is the one of the BSP. */
esp_err_t tab5_power_on_c6(void);
