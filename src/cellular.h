#ifndef CELLULAR_H
#define CELLULAR_H

#include "radar.h"

/* Initialize LTE and provision TLS certificate */
int cellular_init(void);

/* Connect to LTE-M network */
int cellular_connect(void);

/* Disconnect and power down modem */
int cellular_disconnect(void);

/* POST all frames in buffer to webhook */
int cellular_post_frames(const radar_buf_t *buf);

#endif /* CELLULAR_H */