#ifndef RADAR_H
#define RADAR_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum peaks per frame */
#define RADAR_MAX_PEAKS     5
/* Maximum IQ points per frame */
#define RADAR_MAX_IQ_POINTS 256
/* Number of frames to buffer before sending */
#define RADAR_FRAME_BUF_COUNT 7

/* Single radar peak */
typedef struct {
    float distance_m;
    float strength;
} radar_peak_t;

/* Single radar frame — peaks + raw IQ */
typedef struct {
    uint8_t     num_peaks;
    bool        near_edge;
    radar_peak_t peaks[RADAR_MAX_PEAKS];
    uint16_t    num_iq_points;
    int16_t     iq_real[RADAR_MAX_IQ_POINTS];
    int16_t     iq_imag[RADAR_MAX_IQ_POINTS];
    uint32_t    timestamp_ms;
} radar_frame_t;

/* Ring buffer of frames */
typedef struct {
    radar_frame_t frames[RADAR_FRAME_BUF_COUNT];
    uint8_t       count;
    uint8_t       write_idx;
} radar_buf_t;

/* Initialize UART and frame buffer */
int radar_init(void);

/* Trigger one XM125 measurement and store in buffer */
int radar_measure(radar_buf_t *buf);

/* Check if buffer is full (7 frames) */
bool radar_buf_full(const radar_buf_t *buf);

/* Reset buffer after sending */
void radar_buf_reset(radar_buf_t *buf);

#endif /* RADAR_H */