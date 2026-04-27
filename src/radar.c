// #include <zephyr/kernel.h>
// #include <zephyr/logging/log.h>
// #include "radar.h"

// LOG_MODULE_REGISTER(radar, LOG_LEVEL_INF);

// int radar_init(void)
// {
//     LOG_INF("Radar init (stub)");
//     return 0;
// }

// int radar_measure(radar_buf_t *buf)
// {
//     if (buf->count >= RADAR_FRAME_BUF_COUNT) {
//         return -ENOMEM;
//     }

//     radar_frame_t *f = &buf->frames[buf->write_idx];

//     /* Stub: generate dummy measurement */
//     f->timestamp_ms = k_uptime_get_32();
//     f->num_peaks = 2;
//     f->near_edge = false;
//     f->peaks[0].distance_m = 1.234f;
//     f->peaks[0].strength = 15.5f;
//     f->peaks[1].distance_m = 2.567f;
//     f->peaks[1].strength = 8.2f;
//     f->num_iq_points = 0; /* No IQ in stub */

//     buf->write_idx = (buf->write_idx + 1) % RADAR_FRAME_BUF_COUNT;
//     buf->count++;

//     LOG_INF("Stub measurement: dist=%.3f m",
//         (double)f->peaks[0].distance_m);
//     return 0;
// }

// bool radar_buf_full(const radar_buf_t *buf)
// {
//     return buf->count >= RADAR_FRAME_BUF_COUNT;
// }

// void radar_buf_reset(radar_buf_t *buf)
// {
//     buf->count = 0;
//     buf->write_idx = 0;
// }

// //OLD RADAR CODE, SWITCHING TO INTERRUPT-DRIVEN UART READING WITH PROPER JSON
// PARSING #include "radar.h" #include <stdlib.h> #include <string.h> #include
// <zephyr/drivers/uart.h> #include <zephyr/kernel.h> #include
// <zephyr/logging/log.h>

// LOG_MODULE_REGISTER(radar, LOG_LEVEL_INF);

// #define UART_DEVICE_NODE DT_NODELABEL(uart1)
// #define UART_BUF_SIZE 512
// #define UART_TIMEOUT_MS 2000

// static const struct device *uart_dev;
// static uint8_t uart_buf[UART_BUF_SIZE];
// static uint16_t uart_buf_pos;

// static int uart_read_line(char *out, size_t max_len, k_timeout_t timeout) {
//   int64_t deadline = k_uptime_get() + k_ticks_to_ms_floor64(timeout.ticks);
//   size_t pos = 0;

//   while (k_uptime_get() < deadline) {
//     uint8_t c;
//     if (uart_poll_in(uart_dev, &c) == 0) {
//       if (c == '\n') {
//         out[pos] = '\0';
//         return pos;
//       }
//       if (c != '\r' && pos < max_len - 1) {
//         out[pos++] = c;
//       }
//     } else {
//       k_sleep(K_MSEC(1));
//     }
//   }
//   return -ETIMEDOUT;
// }

// int radar_init(void) {
//   uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
//   if (!device_is_ready(uart_dev)) {
//     LOG_ERR("UART device not ready");
//     return -ENODEV;
//   }

//   LOG_INF("Radar UART initialized at 921600 baud");
//   return 0;
// }

// /* Parse a JSON frame from XM125 output.
//  * Expected format (one line):
//  * {"n":2,"near_edge":0,"peaks":[{"d":1.2345,"s":15.20},...],"iq":[[...],...]}\n
//  */
// static int parse_frame(const char *line, radar_frame_t *frame) {
//   memset(frame, 0, sizeof(*frame));
//   frame->timestamp_ms = k_uptime_get_32();

//   /* Parse num_peaks */
//   const char *p = strstr(line, "\"n\":");
//   if (!p)
//     return -EINVAL;
//   frame->num_peaks = atoi(p + 4);
//   if (frame->num_peaks > RADAR_MAX_PEAKS) {
//     frame->num_peaks = RADAR_MAX_PEAKS;
//   }

//   /* Parse near_edge */
//   p = strstr(line, "\"near_edge\":");
//   if (p) {
//     frame->near_edge = atoi(p + 12) != 0;
//   }

//   /* Parse peaks */
//   p = strstr(line, "\"peaks\":[");
//   if (p) {
//     p += 9;
//     for (int i = 0; i < frame->num_peaks; i++) {
//       p = strstr(p, "\"d\":");
//       if (!p)
//         break;
//       frame->peaks[i].distance_m = strtof(p + 4, NULL);

//       p = strstr(p, "\"s\":");
//       if (!p)
//         break;
//       frame->peaks[i].strength = strtof(p + 4, NULL);
//     }
//   }

//   /* Parse IQ data - format: "iq":[[real,imag],...] */
//   p = strstr(line, "\"iq\":[");
//   if (p) {
//     p += 6;
//     int idx = 0;
//     while (idx < RADAR_MAX_IQ_POINTS && *p != ']' && *p != '\0') {
//       /* Skip to next '[' for each IQ pair */
//       p = strchr(p, '[');
//       if (!p)
//         break;
//       p++;
//       frame->iq_real[idx] = (int16_t)atoi(p);
//       p = strchr(p, ',');
//       if (!p)
//         break;
//       p++;
//       frame->iq_imag[idx] = (int16_t)atoi(p);
//       idx++;
//       /* Move past closing ']' of this pair */
//       p = strchr(p, ']');
//       if (!p)
//         break;
//       p++;
//     }
//     frame->num_iq_points = idx;
//   }

//   return 0;
// }

// int radar_measure(radar_buf_t *buf) {
//   if (buf->count >= RADAR_FRAME_BUF_COUNT) {
//     return -ENOMEM;
//   }

//   char line[1024];
//   int len = uart_read_line(line, sizeof(line), K_MSEC(UART_TIMEOUT_MS));
//   if (len < 0) {
//     LOG_WRN("UART timeout waiting for XM125 frame");
//     return len;
//   }
//   if (len == 0) {
//     return -EAGAIN;
//   }

//   LOG_DBG("Received line (%d bytes)", len);

//   radar_frame_t *frame = &buf->frames[buf->write_idx];
//   int err = parse_frame(line, frame);
//   if (err) {
//     LOG_WRN("Failed to parse frame: %d", err);
//     return err;
//   }

//   buf->write_idx = (buf->write_idx + 1) % RADAR_FRAME_BUF_COUNT;
//   buf->count++;

//   LOG_INF("Frame %d: %d peaks, first=%.4f m", buf->count, frame->num_peaks,
//           (double)frame->peaks[0].distance_m);

//   return 0;
// }

// bool radar_buf_full(const radar_buf_t *buf) {
//   return buf->count >= RADAR_FRAME_BUF_COUNT;
// }

// void radar_buf_reset(radar_buf_t *buf) {
//   buf->count = 0;
//   buf->write_idx = 0;
// }

#include "radar.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>


LOG_MODULE_REGISTER(radar, LOG_LEVEL_INF);

#define UART_DEVICE_NODE DT_NODELABEL(uart1)
#define UART_TIMEOUT_MS 15000 /* 15s timeout — longer than XM125 period */
#define LINE_BUF_SIZE 1024

static const struct device *uart_dev;

/* Double buffer — one being filled, one ready to read */
static char line_buf[LINE_BUF_SIZE];
static volatile size_t line_len;
static volatile bool line_ready;

static K_SEM_DEFINE(line_sem, 0, 1);

static void uart_cb(const struct device *dev, void *user_data) {
  ARG_UNUSED(user_data);

  if (!uart_irq_update(dev)) {
    return;
  }

  if (!uart_irq_rx_ready(dev)) {
    return;
  }

  uint8_t c;
  while (uart_fifo_read(dev, &c, 1) == 1) {
    if (c == '\n') {
      line_buf[line_len] = '\0';
      line_ready = true;
      k_sem_give(&line_sem);
    } else if (c != '\r' && line_len < LINE_BUF_SIZE - 1) {
      line_buf[line_len++] = c;
    }
  }
}

int radar_init(void) {
  uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
  if (!device_is_ready(uart_dev)) {
    LOG_ERR("UART device not ready");
    return -ENODEV;
  }

  line_len = 0;
  line_ready = false;

  uart_irq_callback_set(uart_dev, uart_cb);
  uart_irq_rx_enable(uart_dev);

  LOG_INF("Radar UART initialized (interrupt-driven)");
  return 0;
}

static int parse_frame(const char *line, radar_frame_t *frame) {
  memset(frame, 0, sizeof(*frame));
  frame->timestamp_ms = k_uptime_get_32();

  const char *p = strstr(line, "\"n\":");
  if (!p)
    return -EINVAL;
  frame->num_peaks = atoi(p + 4);
  if (frame->num_peaks > RADAR_MAX_PEAKS) {
    frame->num_peaks = RADAR_MAX_PEAKS;
  }

  p = strstr(line, "\"edge\":");
  if (p) {
    frame->near_edge = atoi(p + 7) != 0;
  }

  p = strstr(line, "\"peaks\":[");
  if (p) {
    p += 9;
    for (int i = 0; i < frame->num_peaks; i++) {
      p = strstr(p, "\"d\":");
      if (!p)
        break;
      frame->peaks[i].distance_m = strtof(p + 4, NULL);
      p = strstr(p, "\"s\":");
      if (!p)
        break;
      frame->peaks[i].strength = strtof(p + 4, NULL);
    }
  }

  p = strstr(line, "\"iq\":[");
  if (p) {
    p += 6;
    int idx = 0;
    while (idx < RADAR_MAX_IQ_POINTS && *p != ']' && *p != '\0') {
      p = strchr(p, '[');
      if (!p)
        break;
      p++;
      frame->iq_real[idx] = (int16_t)atoi(p);
      p = strchr(p, ',');
      if (!p)
        break;
      p++;
      frame->iq_imag[idx] = (int16_t)atoi(p);
      idx++;
      p = strchr(p, ']');
      if (!p)
        break;
      p++;
    }
    frame->num_iq_points = idx;
  }

  return 0;
}

int radar_measure(radar_buf_t *buf) {
  if (buf->count >= RADAR_FRAME_BUF_COUNT) {
    return -ENOMEM;
  }

  /* Reset for next line */
  line_len = 0;
  line_ready = false;

  /* Sleep until XM125 sends a line or timeout */
  int err = k_sem_take(&line_sem, K_MSEC(UART_TIMEOUT_MS));
  if (err) {
    LOG_WRN("UART timeout waiting for XM125 frame");
    return -ETIMEDOUT;
  }

  radar_frame_t *frame = &buf->frames[buf->write_idx];
  err = parse_frame(line_buf, frame);
  if (err) {
    LOG_WRN("Failed to parse frame: %d", err);
    return err;
  }

  buf->write_idx = (buf->write_idx + 1) % RADAR_FRAME_BUF_COUNT;
  buf->count++;

  LOG_INF("Frame %d: %d peaks, first=%.4f m", buf->count, frame->num_peaks,
          (double)frame->peaks[0].distance_m);

  return 0;
}

bool radar_buf_full(const radar_buf_t *buf) {
  return buf->count >= RADAR_FRAME_BUF_COUNT;
}

void radar_buf_reset(radar_buf_t *buf) {
  buf->count = 0;
  buf->write_idx = 0;
}
