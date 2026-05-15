// #include "cellular.h"
// #include "radar.h"
// #include <modem/lte_lc.h>
// #include <modem/nrf_modem_lib.h>
// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

// #define MEASURE_INTERVAL_MS 8571

// static radar_buf_t radar_buf;

// int main(void) {
//   int err;

//   /* Init modem library first */
//   err = nrf_modem_lib_init();
//   if (err) {
//     LOG_ERR("nrf_modem_lib_init failed: %d", err);
//     return err;
//   }

//   /* Graceful power off - clears reset loop counter */
//   lte_lc_func_mode_set(LTE_LC_FUNC_MODE_POWER_OFF);
//   k_sleep(K_MSEC(500));
//   nrf_modem_lib_shutdown();
//   k_sleep(K_MSEC(500));

//   LOG_INF("Radar application starting");

//   err = radar_init();
//   if (err) {
//     LOG_ERR("radar_init failed: %d", err);
//     return err;
//   }

//   err = cellular_init();
//   if (err) {
//     LOG_ERR("cellular_init failed: %d", err);
//     return err;
//   }

//   radar_buf_reset(&radar_buf);

//   while (true) {
//     err = radar_measure(&radar_buf);
//     if (err) {
//       LOG_WRN("radar_measure failed: %d, skipping", err);
//     } else {
//       LOG_INF("Measurement %d/%d stored", radar_buf.count,
//               RADAR_FRAME_BUF_COUNT);
//     }

//     if (radar_buf_full(&radar_buf)) {
//       LOG_INF("Buffer full, connecting and sending");

//       err = cellular_connect();
//       if (err) {
//         LOG_ERR("cellular_connect failed: %d", err);
//       } else {
//         err = cellular_post_frames(&radar_buf);
//         if (err) {
//           LOG_ERR("cellular_post_frames failed: %d", err);
//         } else {
//           LOG_INF("Frames sent successfully");
//         }
//         cellular_disconnect();
//       }

//       radar_buf_reset(&radar_buf);
//     }

//     // k_sleep(K_MSEC(MEASURE_INTERVAL_MS));
//   }

//   return 0;
// }
// ^^^^^^^^^^^ OLD WORKING VERSION ^^^^^^^^^^^

#include "cellular.h"
#include "config.h"
#include "uart.h"
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

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

    // Sync time via SNTP — must happen after LTE connect, once per boot
    err = cellular_sntp_sync();
    if (err) {
        // Non-fatal — frames will have timestamp -1, backend can flag them
        LOG_WRN("SNTP sync failed: %d — timestamps will be invalid", err);
    }

    LOG_INF("Boot complete — entering main loop (update rate: %d ms)",
            UPDATE_RATE_MS);

    // ---------------------------------------------------------------------------
    // Main loop — nRF9151 paces the system
    // ---------------------------------------------------------------------------

    while (true) {
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
