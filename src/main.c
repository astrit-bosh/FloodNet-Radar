#include "cellular.h"
#include "radar.h"
#include <modem/lte_lc.h>
#include <modem/nrf_modem_lib.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_INF);

#define MEASURE_INTERVAL_MS 8571

static radar_buf_t radar_buf;

int main(void) {
  int err;

  /* Init modem library first */
  err = nrf_modem_lib_init();
  if (err) {
    LOG_ERR("nrf_modem_lib_init failed: %d", err);
    return err;
  }

  /* Graceful power off - clears reset loop counter */
  lte_lc_func_mode_set(LTE_LC_FUNC_MODE_POWER_OFF);
  k_sleep(K_MSEC(500));
  nrf_modem_lib_shutdown();
  k_sleep(K_MSEC(500));

  LOG_INF("Radar application starting");

  err = radar_init();
  if (err) {
    LOG_ERR("radar_init failed: %d", err);
    return err;
  }

  err = cellular_init();
  if (err) {
    LOG_ERR("cellular_init failed: %d", err);
    return err;
  }

  radar_buf_reset(&radar_buf);

  while (true) {
    err = radar_measure(&radar_buf);
    if (err) {
      LOG_WRN("radar_measure failed: %d, skipping", err);
    } else {
      LOG_INF("Measurement %d/%d stored", radar_buf.count,
              RADAR_FRAME_BUF_COUNT);
    }

    if (radar_buf_full(&radar_buf)) {
      LOG_INF("Buffer full, connecting and sending");

      err = cellular_connect();
      if (err) {
        LOG_ERR("cellular_connect failed: %d", err);
      } else {
        err = cellular_post_frames(&radar_buf);
        if (err) {
          LOG_ERR("cellular_post_frames failed: %d", err);
        } else {
          LOG_INF("Frames sent successfully");
        }
        cellular_disconnect();
      }

      radar_buf_reset(&radar_buf);
    }

    // k_sleep(K_MSEC(MEASURE_INTERVAL_MS));
  }

  return 0;
}
