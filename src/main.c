#include "cellular.h"
#include "config.h"
#include "uart.h"
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/watchdog.h>

#define WDT_NODE DT_ALIAS(watchdog0)
#define WDT_TIMEOUT_MS 300000U  // 5 minutes — longer than one full cycle
LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

static const struct device *wdt;
static int wdt_channel_id;

static int watchdog_init(void)
{
    wdt = DEVICE_DT_GET(WDT_NODE);
    if (!device_is_ready(wdt)) {
        LOG_ERR("Watchdog device not ready");
        return -ENODEV;
    }

    struct wdt_timeout_cfg wdt_config = {
        .flags = WDT_FLAG_RESET_SOC,
        .window.min = 0,
        .window.max = WDT_TIMEOUT_MS,
    };

    wdt_channel_id = wdt_install_timeout(wdt, &wdt_config);
    if (wdt_channel_id < 0) {
        LOG_ERR("Watchdog install timeout failed: %d", wdt_channel_id);
        return wdt_channel_id;
    }

    int err = wdt_setup(wdt, WDT_OPT_PAUSE_HALTED_BY_DBG);
    if (err) {
        LOG_ERR("Watchdog setup failed: %d", err);
        return err;
    }

    LOG_INF("Watchdog initialized (timeout: %d ms)", WDT_TIMEOUT_MS);
    return 0;
}



// ---------------------------------------------------------------------------
// WAKE GPIO — nRF9151 drives this pin to wake/sleep XM125
// Defined in devicetree overlay as wake_pin alias
// ---------------------------------------------------------------------------

static const struct gpio_dt_spec wake_pin = GPIO_DT_SPEC_GET(WAKE_PIN_NODE, gpios);

static int wake_pin_init(void)
{
    if (!gpio_is_ready_dt(&wake_pin)) {
        LOG_ERR("WAKE GPIO device not ready");
        return -ENODEV;
    }
    // Start deasserted — XM125 stays in hibernate until we're ready
    return gpio_pin_configure_dt(&wake_pin, GPIO_OUTPUT_INACTIVE);
}

static void wake_xm125(void)
{
    gpio_pin_set_dt(&wake_pin, 1);
    LOG_DBG("WAKE asserted");
}

static void sleep_xm125(void)
{
    gpio_pin_set_dt(&wake_pin, 0);
    LOG_DBG("WAKE deasserted — XM125 hibernating");
}

// ---------------------------------------------------------------------------
// Frame buffer
// ---------------------------------------------------------------------------

static radar_buf_t radar_buf;

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void)
{

    int err;

    LOG_INF("Flood sensor starting");

    // Graceful modem power-off — clears any prior reset loop state
    err = nrf_modem_lib_init();
    if (err) {
        LOG_ERR("nrf_modem_lib_init failed: %d", err);
        return err;
    }
    lte_lc_func_mode_set(LTE_LC_FUNC_MODE_POWER_OFF);
    k_sleep(K_MSEC(500));
    nrf_modem_lib_shutdown();
    k_sleep(K_MSEC(500));

    // Initialize WAKE GPIO — XM125 stays asleep until first cycle
    err = wake_pin_init();
    if (err) {
        LOG_ERR("wake_pin_init failed: %d", err);
        return err;
    }

    err = watchdog_init();
    if (err) {
        LOG_WRN("Watchdog init failed: %d — continuing without watchdog", err);
    }

    // Initialize UART RX ring buffer and INT GPIO interrupt
    err = uart_init();
    if (err) {
        LOG_ERR("uart_init failed: %d", err);
        return err;
    }

    // Initialize modem and provision TLS certificate
    err = cellular_init();
    if (err) {
        LOG_ERR("cellular_init failed: %d", err);
        return err;
    }

    // Connect to LTE-M and configure eDRX
    err = cellular_connect();
    if (err) {
        LOG_ERR("cellular_connect failed: %d", err);
        return err;
    }

    // Sync time via SNTP — must happen after LTE connect, retry 3 times
    for (int i = 0; i < 3; i++) {
        err = cellular_sntp_sync();
        if (err == 0) {
            break;
        }
        LOG_WRN("SNTP attempt %d/3 failed (%d), retrying in 5s...", i + 1, err);
        if (i < 2) {
            k_sleep(K_SECONDS(5));
        }
    }
    if (err) {
        LOG_ERR("SNTP sync failed after 3 attempts — timestamps will be invalid");
    }

    LOG_INF("Boot complete — entering main loop (update rate: %d ms)",
            UPDATE_RATE_MS);

    // ---------------------------------------------------------------------------
    // Main loop — nRF9151 paces the system
    // ---------------------------------------------------------------------------

    while (true) {

        // Kick watchdog — must happen at least every WDT_TIMEOUT_MS
        wdt_feed(wdt, wdt_channel_id);

        int64_t cycle_start_ms = k_uptime_get();

        // 1. Wake XM125
        wake_xm125();

        // 2. Wait for XM125 to assert INT (all frames in UART buffer)
        //    UART ring buffer fills passively while we wait
        err = uart_read_frames(&radar_buf);
        if (err) {
            LOG_ERR("uart_read_frames failed: %d", err);
            // Deassert WAKE so XM125 can hibernate even on error
            sleep_xm125();
            uart_buf_reset(&radar_buf);
            goto next_cycle;
        }

        // 3. Deassert WAKE — XM125 hibernates while we do LTE POST
        sleep_xm125();

        // 4. POST frames over LTE
        LOG_INF("Posting %d frames", radar_buf.count);
        err = cellular_post_frames(&radar_buf);
        if (err) {
            LOG_ERR("cellular_post_frames failed: %d", err);
        } else {
            LOG_INF("POST complete");
        }

        uart_buf_reset(&radar_buf);

next_cycle:
        // 5. Sleep for remainder of UPDATE_RATE_MS
        //    eDRX keeps modem registered during this sleep
        int64_t elapsed_ms = k_uptime_get() - cycle_start_ms;
        int64_t sleep_ms   = (int64_t)UPDATE_RATE_MS - elapsed_ms;

        if (sleep_ms > 0) {
            LOG_DBG("Sleeping %lld ms until next cycle", sleep_ms);
            k_sleep(K_MSEC(sleep_ms));
        } else {
            LOG_WRN("Cycle overran UPDATE_RATE_MS by %lld ms", -sleep_ms);
        }
    }

    return 0;
}
