#ifndef CONFIG_H
#define CONFIG_H

#include <zephyr/kernel.h>

// ---------------------------------------------------------------------------
// Timing
// ---------------------------------------------------------------------------

// How often the nRF9151 wakes the XM125 and collects a buffer of frames
// Default: 1 minute. Increase for lower power consumption.
#define UPDATE_RATE_MS          60000U

// ---------------------------------------------------------------------------
// Radar / UART
// ---------------------------------------------------------------------------

// Number of frames to collect per wake cycle before posting
// Must be a multiple of 7 (XM125 takes 7 measurements per wake)
#define FRAMES_PER_BUFFER       7U

// Maximum peaks reported per frame (matches Google Sheet fixed columns)
#define MAX_PEAKS_PER_FRAME     3U

// UART ring buffer size — must hold all frames before CPU wakes on INT
// Each frame is at most ~40 bytes ASCII (3 peaks worst case)
// 7 frames * 40 bytes = ~280 bytes — 512 gives comfortable margin
#define UART_RX_BUF_SIZE        512U

// UART timeout waiting for all frames after INT fires (ms)
#define UART_FRAME_TIMEOUT_MS   5000U

// ---------------------------------------------------------------------------
// GPIO pins
// ---------------------------------------------------------------------------

// WAKE pin — nRF9151 asserts high to wake XM125, deasserts after UART read
#define WAKE_PIN_NODE           DT_ALIAS(wake_pin)

// INT pin — XM125 asserts high when all frames are in UART buffer
#define INT_PIN_NODE            DT_ALIAS(int_pin)

// ---------------------------------------------------------------------------
// Cellular / POST
// ---------------------------------------------------------------------------

#define HTTPS_PORT              "443"
#define WEBHOOK_HOST            "script.google.com"
#define WEBHOOK_UUID            "macros/s/AKfycbwgBOoHvD2sKEsbWD8YyxhMu8Nycs5IzqO6mXfx4eeInN-su6hR5VLARMVwtdEtNbTTpg/exec"
#define TLS_SEC_TAG             42

// POST body buffer — static, power of 2
// Each frame ~130 bytes worst case flat JSON
// 4096 fits up to ~31 frames comfortably
#define POST_BODY_SIZE          4096U
#define POST_HEADER_SIZE        512U
#define RECV_BUF_SIZE           2048U


// LTE connection timeout (ms)
#define LTE_CONNECT_TIMEOUT_MS  180000U

// SNTP server
#define SNTP_SERVER             "time.cloudflare.com"
#define SNTP_TIMEOUT_MS         5000U

// ---------------------------------------------------------------------------
// Compile-time guards
// ---------------------------------------------------------------------------

BUILD_ASSERT(FRAMES_PER_BUFFER % 7 == 0,
             "FRAMES_PER_BUFFER must be a multiple of 7");

BUILD_ASSERT(FRAMES_PER_BUFFER * 130 < POST_BODY_SIZE,
             "POST_BODY_SIZE too small for FRAMES_PER_BUFFER — increase POST_BODY_SIZE");

BUILD_ASSERT(FRAMES_PER_BUFFER * 40 < UART_RX_BUF_SIZE,
             "UART_RX_BUF_SIZE too small for FRAMES_PER_BUFFER — increase UART_RX_BUF_SIZE");

#endif /* CONFIG_H */
