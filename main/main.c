/* SPDX-License-Identifier: Apache-2.0
 *
 * Updates the firmware of the ESP32-C6 of the M5Stack Tab5 over the SDIO link it shares with the ESP32-P4.
 *
 * The ESP-Hosted co-processor firmware is embedded in this application (firmware/network_adapter.bin). After
 * the C6 has been powered and the SDIO transport is up, the image is sent with ESP-Hosted's OTA calls (begin, write
 * in chunks, end, activate). The Wi-Fi stack is never started, so nothing else uses the link.
 *
 * Based on the CrowPanel ESP32-P4 / C6 SDIO OTA tool by lboshuizen (Apache-2.0), itself derived from Espressif's
 * ESP-Hosted OTA example (Apache-2.0).
 */
#include <inttypes.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "esp_err.h"
#include "esp_event.h"
#include "esp_hosted.h"
#include "esp_hosted_ota.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "tab5_power.h"

#ifdef CONFIG_TAB5_C6_OTA_FORCE
#define OTA_FORCE 1
#else
#define OTA_FORCE 0
#endif

static const char *TAG = "tab5-c6-ota";

extern const uint8_t fw_start[] asm("_binary_network_adapter_bin_start");
extern const uint8_t fw_end[] asm("_binary_network_adapter_bin_end");

#define CHIP_ID_ESP32C6 13
#define APP_DESC_OFFSET 32 /* image header (24 bytes) + first segment header (8 bytes) */
#define APP_DESC_MAGIC  0xABCD5432

static int64_t now_ms(void)
{
    return esp_timer_get_time() / 1000;
}

/* What the image says about itself, from the app description at the start of the first segment */
typedef struct
{
    char version[33];
    char project[33];
    bool has_desc;
} image_info_t;

static bool check_image(image_info_t *info)
{
    const size_t len = fw_end - fw_start;
    memset(info, 0, sizeof(*info));

    if (len < APP_DESC_OFFSET + 80 || fw_start[0] != 0xE9)
    {
        ESP_LOGE(TAG, "[FAIL] firmware/network_adapter.bin is not an ESP32 app image (%zu bytes)", len);
        return false;
    }
    const unsigned chip_id = fw_start[12] | (fw_start[13] << 8);
    if (chip_id != CHIP_ID_ESP32C6)
    {
        ESP_LOGE(TAG, "[FAIL] The image is for chip id %u, the Tab5 needs an ESP32-C6 (%d)", chip_id, CHIP_ID_ESP32C6);
        return false;
    }
    if (len > CONFIG_TAB5_C6_OTA_MAX_IMAGE_SIZE)
    {
        ESP_LOGE(TAG, "[FAIL] The image is %zu bytes, the C6 only has room for %d", len, CONFIG_TAB5_C6_OTA_MAX_IMAGE_SIZE);
        return false;
    }
    if (fw_start[2] == 0 || fw_start[2] == 1)
    {
        ESP_LOGW(TAG, "[WARN] The image was built for QIO/QOUT flash mode, the C6 modules are normally used in DIO");
    }

    const uint8_t *desc = fw_start + APP_DESC_OFFSET;
    uint32_t magic = desc[0] | (desc[1] << 8) | (desc[2] << 16) | ((uint32_t)desc[3] << 24);
    if (magic == APP_DESC_MAGIC)
    {
        info->has_desc = true;
        memcpy(info->version, desc + 16, 32);
        memcpy(info->project, desc + 48, 32);
        ESP_LOGI(TAG, "[DIAG] Image: %s, version %s, %zu bytes", info->project, info->version, len);
        /* "network_adapter" in older releases, "eh_cp_..." in the 3.x examples and builds */
        if (strcmp(info->project, "network_adapter") != 0 && strncmp(info->project, "eh_cp_", 6) != 0)
            ESP_LOGW(TAG, "[WARN] This doesn't look like the ESP-Hosted co-processor firmware");
    }
    else
    {
        ESP_LOGW(TAG, "[WARN] No app description in the image, can't tell its version (%zu bytes)", len);
    }
    return true;
}

static bool read_version(esp_hosted_coprocessor_fwver_t *ver)
{
    memset(ver, 0, sizeof(*ver));
    return esp_hosted_get_coprocessor_fwversion(ver) == ESP_OK;
}

static bool is_host_version(const esp_hosted_coprocessor_fwver_t *ver)
{
    return ver->major1 == ESP_HOSTED_VERSION_MAJOR_1 && ver->minor1 == ESP_HOSTED_VERSION_MINOR_1
           && ver->patch1 == ESP_HOSTED_VERSION_PATCH_1;
}

static esp_err_t transfer_image(void)
{
    const size_t len = fw_end - fw_start;
    const size_t chunk = CONFIG_TAB5_C6_OTA_CHUNK_SIZE;

    esp_err_t err = esp_hosted_slave_ota_begin();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "[FAIL] OTA begin: %s (0x%x)", esp_err_to_name(err), err);
        return err;
    }

    int next_report = 10;
    for (size_t off = 0; off < len;)
    {
        size_t n = len - off < chunk ? len - off : chunk;
        err = esp_hosted_slave_ota_write(fw_start + off, n);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "[FAIL] OTA write at %zu/%zu: %s (0x%x)", off, len, esp_err_to_name(err), err);
            return err;
        }
        off += n;
        if ((int)(off * 100 / len) >= next_report)
        {
            ESP_LOGI(TAG, "[DIAG] %d%% sent", next_report);
            next_report += 10;
        }
    }

    err = esp_hosted_slave_ota_end();
    if (err != ESP_OK)
        ESP_LOGE(TAG, "[FAIL] OTA end: %s (0x%x)", esp_err_to_name(err), err);
    return err;
}

static void halt(void)
{
    ESP_LOGW(TAG, "Done. Flash the normal firmware again (the C6 keeps its new firmware) or reset to run this again.");
    while (1)
        vTaskDelay(pdMS_TO_TICKS(10000));
}

void app_main(void)
{
    ESP_LOGW(TAG, "==========================================================");
    ESP_LOGW(TAG, "  M5Stack Tab5: update of the ESP32-C6 firmware (SDIO)");
    ESP_LOGW(TAG, "  Host ESP-Hosted %d.%d.%d, Wi-Fi is not started", ESP_HOSTED_VERSION_MAJOR_1,
             ESP_HOSTED_VERSION_MINOR_1, ESP_HOSTED_VERSION_PATCH_1);
    ESP_LOGW(TAG, "==========================================================");

    image_info_t image;
    if (!check_image(&image))
        halt();

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGW(TAG, "[PHASE] 1/4 power and connect t=%" PRId64 "ms", now_ms());
    err = tab5_power_on_c6();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "[FAIL] Could not switch on the C6 through the IO expander: %s", esp_err_to_name(err));
        halt();
    }
    err = esp_hosted_init();
    if (err == ESP_OK)
        err = esp_hosted_connect_to_slave();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "[FAIL] No answer from the C6: %s (0x%x)", esp_err_to_name(err), err);
        ESP_LOGE(TAG, "[DIAG] The factory firmware (1.4.1) may be too old to talk to this host version. If the C6 never");
        ESP_LOGE(TAG, "[DIAG] answers it can only be flashed over its download mode, see README.md");
        halt();
    }
    ESP_LOGI(TAG, "[PASS] Connected to the C6 at t=%" PRId64 "ms", now_ms());

    ESP_LOGW(TAG, "[PHASE] 2/4 version t=%" PRId64 "ms", now_ms());
    esp_hosted_coprocessor_fwver_t ver;
    if (read_version(&ver))
    {
        ESP_LOGI(TAG, "[DIAG] C6 firmware %" PRIu32 ".%" PRIu32 ".%" PRIu32, ver.major1, ver.minor1, ver.patch1);
        if (is_host_version(&ver) && !OTA_FORCE)
        {
            ESP_LOGI(TAG, "[PASS] The C6 already runs the version of the host, nothing to do");
            halt();
        }
    }
    else
    {
        ESP_LOGW(TAG, "[WARN] Could not read the C6 version (old firmware?), updating anyway");
    }

    ESP_LOGW(TAG, "[PHASE] 3/4 transfer t=%" PRId64 "ms", now_ms());
    const int64_t start = now_ms();
    err = transfer_image();
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "[FAIL] Transfer failed after %" PRId64 "ms. The running firmware of the C6 was not touched", now_ms() - start);
        ESP_LOGE(TAG, "[DIAG] Try a lower SDIO clock (CONFIG_ESP_HOSTED_HOST_SDIO_CLK_KHZ) or a smaller chunk size");
        halt();
    }
    ESP_LOGI(TAG, "[PASS] Image sent in %" PRId64 "ms", now_ms() - start);

    err = esp_hosted_slave_ota_activate();
    if (err != ESP_OK)
    {
        /* Co-processor firmware older than 2.6 activates the image when the transfer ends, it has no separate call */
        ESP_LOGW(TAG, "[WARN] Activate: %s (0x%x). Normal with a very old co-processor firmware, checking below", esp_err_to_name(err), err);
    }

    ESP_LOGW(TAG, "[PHASE] 4/4 verify t=%" PRId64 "ms", now_ms());
    vTaskDelay(pdMS_TO_TICKS(CONFIG_TAB5_C6_OTA_VERIFY_DELAY_MS));
    if (read_version(&ver))
    {
        ESP_LOGI(TAG, "[DIAG] C6 firmware now %" PRIu32 ".%" PRIu32 ".%" PRIu32, ver.major1, ver.minor1, ver.patch1);
        if (is_host_version(&ver))
            ESP_LOGI(TAG, "[PASS] *** The C6 runs the firmware of the host ***");
        else
            ESP_LOGW(TAG, "[WARN] The version isn't the one of the host (%d.%d.%d), is it the image you embedded?",
                     ESP_HOSTED_VERSION_MAJOR_1, ESP_HOSTED_VERSION_MINOR_1, ESP_HOSTED_VERSION_PATCH_1);
    }
    else
    {
        ESP_LOGW(TAG, "[WARN] Could not read the version after the update, it may still be rebooting");
        ESP_LOGW(TAG, "[DIAG] Run this again: it says whether the C6 now has the version of the host");
    }
    halt();
}
