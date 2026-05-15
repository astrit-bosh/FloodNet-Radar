#ifndef UART_H
#define UART_H

#include "config.h"
#include <stdbool.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

// Single radar peak
typedef struct {
    float distance_m;
    float strength_db;
} radar_peak_t;

// Single parsed radar frame
// A frame with num_peaks == 0 is a valid no-detection frame (XM125 sent "0\n")
typedef struct {
    uint8_t      num_peaks;
    radar_peak_t peaks[MAX_PEAKS_PER_FRAME];
    int64_t      timestamp_ms; // Unix time ms, set by nRF9151 after SNTP sync
} radar_frame_t;

// Buffer of frames collected per wake cycle
typedef struct {
    radar_frame_t frames[FRAMES_PER_BUFFER];
    uint8_t       count;
} radar_buf_t;

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

// Initialize UART RX with DMA ring buffer and INT GPIO interrupt
int uart_init(void);

// Read and parse FRAMES_PER_BUFFER lines from ring buffer into buf
// Blocks until all frames received or UART_FRAME_TIMEOUT_MS elapses
// Timestamps each frame using current SNTP-anchored Unix time
int uart_read_frames(radar_buf_t *buf);

// Reset frame buffer
void uart_buf_reset(radar_buf_t *buf);

#endif /* UART_H */
