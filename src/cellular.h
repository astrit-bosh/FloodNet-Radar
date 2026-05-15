// #ifndef CELLULAR_H
// #define CELLULAR_H

// #include "radar.h"

// /* Initialize LTE and provision TLS certificate */
// int cellular_init(void);

// /* Connect to LTE-M network */
// int cellular_connect(void);

// /* Disconnect and power down modem */
// int cellular_disconnect(void);

// /* POST all frames in buffer to webhook */
// int cellular_post_frames(const radar_buf_t *buf);

// #endif /* CELLULAR_H */
// ^^^^^^^^^^^ OLD WORKING VERSION ^^^^^^^^^^^

#ifndef CELLULAR_H
#define CELLULAR_H

#include "uart.h"
#include <stdint.h>

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------

// Initialize modem library and provision TLS certificate
// Must be called once at boot before cellular_connect()
int cellular_init(void);

// ---------------------------------------------------------------------------
// Connection
// ---------------------------------------------------------------------------

// Connect to LTE-M, configure eDRX (fallback to light sleep if unsupported)
// Blocks until connected or LTE_CONNECT_TIMEOUT_MS elapses
int cellular_connect(void);

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

// Sync time via SNTP — call once after cellular_connect() on every boot
// Stores Unix time anchor internally
int cellular_sntp_sync(void);

// Returns current Unix time in milliseconds, anchored to last SNTP sync
// Returns -1 if SNTP has not been synced yet
int64_t cellular_get_unix_time_ms(void);

// ---------------------------------------------------------------------------
// Data
// ---------------------------------------------------------------------------

// POST all frames in buf to Google Apps Script endpoint over HTTPS
int cellular_post_frames(const radar_buf_t *buf);

#endif /* CELLULAR_H */
