#include "uart.h"
#include "cellular.h"
#include <stdlib.h>
#include <string.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(uart, LOG_LEVEL_INF);

// ---------------------------------------------------------------------------
// Device bindings
// ---------------------------------------------------------------------------

#define UART_DEVICE_NODE DT_NODELABEL(uart1)

static const struct device *uart_dev;
static const struct gpio_dt_spec int_pin = GPIO_DT_SPEC_GET(INT_PIN_NODE, gpios);

// ---------------------------------------------------------------------------
// Ring buffer
// Filled by UART RX interrupt while CPU sleeps in eDRX idle
// Drained by uart_read_frames() after INT fires
// ---------------------------------------------------------------------------

static uint8_t  rx_ring_buf[UART_RX_BUF_SIZE];
static uint32_t rx_head; // written by ISR
static uint32_t rx_tail; // read by uart_read_frames()

// INT pin interrupt semaphore — given when XM125 asserts INT
static K_SEM_DEFINE(int_sem, 0, 1);

// ---------------------------------------------------------------------------
// INT pin GPIO interrupt handler
// XM125 asserts INT when all FRAMES_PER_BUFFER frames are in UART TX buffer
// ---------------------------------------------------------------------------

static struct gpio_callback int_cb_data;

static void int_pin_handler(const struct device *dev, struct gpio_callback *cb,
                            uint32_t pins)
{
    ARG_UNUSED(dev);
    ARG_UNUSED(cb);
    ARG_UNUSED(pins);
    k_sem_give(&int_sem);
}

// ---------------------------------------------------------------------------
// UART RX interrupt handler
// Runs while CPU is in eDRX idle — no semaphores, just ring buffer fill
// ---------------------------------------------------------------------------

static void uart_irq_handler(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) {
        return;
    }

    if (!uart_irq_rx_ready(dev)) {
        return;
    }

    uint8_t c;
    while (uart_fifo_read(dev, &c, 1) == 1) {
        uint32_t next_head = (rx_head + 1) % UART_RX_BUF_SIZE;
        if (next_head != rx_tail) {
            // Buffer not full — store byte
            rx_ring_buf[rx_head] = c;
            rx_head = next_head;
        } else {
            // Buffer overflow — drop byte, log will fire on next CPU wake
            LOG_WRN("UART ring buffer overflow");
        }
    }
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

int uart_init(void)
{
    int err;

    // UART
    uart_dev = DEVICE_DT_GET(UART_DEVICE_NODE);
    if (!device_is_ready(uart_dev)) {
        LOG_ERR("UART device not ready");
        return -ENODEV;
    }

    rx_head = 0;
    rx_tail = 0;

    uart_irq_callback_set(uart_dev, uart_irq_handler);
    uart_irq_rx_enable(uart_dev);

    // INT pin — input, interrupt on rising edge (XM125 asserts high)
    if (!gpio_is_ready_dt(&int_pin)) {
        LOG_ERR("INT GPIO device not ready");
        return -ENODEV;
    }

    err = gpio_pin_configure_dt(&int_pin, GPIO_INPUT);
    if (err) {
        LOG_ERR("INT pin configure failed: %d", err);
        return err;
    }

    err = gpio_pin_interrupt_configure_dt(&int_pin, GPIO_INT_EDGE_RISING);
    if (err) {
        LOG_ERR("INT pin interrupt configure failed: %d", err);
        return err;
    }

    gpio_init_callback(&int_cb_data, int_pin_handler, BIT(int_pin.pin));
    gpio_add_callback(int_pin.port, &int_cb_data);

    LOG_INF("UART and INT pin initialized");
    return 0;
}

// ---------------------------------------------------------------------------
// Ring buffer helpers
// ---------------------------------------------------------------------------

static bool ring_buf_empty(void)
{
    return rx_head == rx_tail;
}

static int ring_buf_get_byte(uint8_t *c)
{
    if (ring_buf_empty()) {
        return -1;
    }
    *c = rx_ring_buf[rx_tail];
    rx_tail = (rx_tail + 1) % UART_RX_BUF_SIZE;
    return 0;
}

// ---------------------------------------------------------------------------
// Frame line parser
//
// Parses one UART line into a radar_frame_t.
//
// Input formats:
//   "0"                          — no detection
//   "d1,s1"                      — 1 peak
//   "d1,s1 d2,s2"                — 2 peaks
//   "d1,s1 d2,s2 d3,s3"          — 3 peaks
// ---------------------------------------------------------------------------

static int parse_frame_line(const char *line, radar_frame_t *frame)
{
    memset(frame, 0, sizeof(*frame));

    // No detection
    if (line[0] == '0' && (line[1] == '\0' || line[1] == '\r')) {
        frame->num_peaks = 0;
        return 0;
    }

    const char *p = line;
    uint8_t peak_idx = 0;

    while (*p != '\0' && peak_idx < MAX_PEAKS_PER_FRAME) {
        char *end;

        // Parse distance
        float distance = strtof(p, &end);
        if (end == p || *end != ',') {
            // Not a valid distance,strength pair
            break;
        }
        p = end + 1; // skip comma

        // Parse strength
        float strength = strtof(p, &end);
        if (end == p) {
            break;
        }

        frame->peaks[peak_idx].distance_m  = distance;
        frame->peaks[peak_idx].strength_db = strength;
        peak_idx++;

        p = end;

        // Skip space separator between peaks
        if (*p == ' ') {
            p++;
        }
    }

    frame->num_peaks = peak_idx;
    return 0;
}

// ---------------------------------------------------------------------------
// Read and parse FRAMES_PER_BUFFER frames from ring buffer
//
// Blocks waiting for INT pin semaphore first, then drains ring buffer.
// Timestamps each frame using SNTP-anchored Unix time from cellular.c.
// ---------------------------------------------------------------------------

int uart_read_frames(radar_buf_t *buf)
{
    uart_buf_reset(buf);

    // Wait for XM125 to assert INT — all frames now in ring buffer
    int err = k_sem_take(&int_sem, K_MSEC(UART_FRAME_TIMEOUT_MS));
    if (err) {
        LOG_ERR("Timeout waiting for XM125 INT signal");
        return -ETIMEDOUT;
    }

    LOG_INF("INT received, reading %d frames from ring buffer", FRAMES_PER_BUFFER);

    // Drain ring buffer line by line
    char line[64];
    uint8_t line_pos = 0;
    uint8_t frames_parsed = 0;

    while (frames_parsed < FRAMES_PER_BUFFER) {
        uint8_t c;

        if (ring_buf_get_byte(&c) < 0) {
            // Ring buffer exhausted before we got all frames
            LOG_WRN("Ring buffer exhausted after %d frames", frames_parsed);
            break;
        }

        if (c == '\n') {
            line[line_pos] = '\0';
            line_pos = 0;

            if (strlen(line) == 0) {
                continue; // skip empty lines
            }

            radar_frame_t *frame = &buf->frames[frames_parsed];
            err = parse_frame_line(line, frame);
            if (err) {
                LOG_WRN("Failed to parse frame line: %s", line);
                continue;
            }

            // Timestamp with SNTP-anchored Unix time
            frame->timestamp_ms = cellular_get_unix_time_ms();

            LOG_INF("Frame %d: %d peaks", frames_parsed + 1, frame->num_peaks);
            frames_parsed++;
            buf->count = frames_parsed;

        } else if (c != '\r' && line_pos < sizeof(line) - 1) {
            line[line_pos++] = c;
        }
    }

    if (frames_parsed < FRAMES_PER_BUFFER) {
        LOG_WRN("Only parsed %d/%d frames", frames_parsed, FRAMES_PER_BUFFER);
    }

    return (frames_parsed > 0) ? 0 : -EIO;
}

// ---------------------------------------------------------------------------
// Reset buffer
// ---------------------------------------------------------------------------

void uart_buf_reset(radar_buf_t *buf)
{
    memset(buf, 0, sizeof(*buf));
}
