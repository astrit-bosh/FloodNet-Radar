#include "cellular.h"
#include <modem/lte_lc.h>
#include <modem/modem_key_mgmt.h>
#include <nrf_modem_at.h>
#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/conn_mgr_connectivity.h>
#include <zephyr/net/conn_mgr_monitor.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>

LOG_MODULE_REGISTER(cellular, LOG_LEVEL_INF);

#define HTTPS_PORT "443"
#define WEBHOOK_HOST "script.google.com"
// #define WEBHOOK_UUID    "33f5c8f6-0bc0-4427-9849-ef2d59dc8bf7" ASRC Mac
#define WEBHOOK_UUID                                                           \
  "macros/s/"                                                                  \
  "AKfycbwgBOoHvD2sKEsbWD8YyxhMu8Nycs5IzqO6mXfx4eeInN-"                        \
  "su6hR5VLARMVwtdEtNbTTpg/exec" // Google Apps Script URL
#define TLS_SEC_TAG 42
#define RECV_BUF_SIZE 2048

static char recv_buf[RECV_BUF_SIZE];
// static char post_buf[4096];
static K_SEM_DEFINE(lte_connected_sem, 0, 1);

static const char cert[] = {
#include "DigiCertGlobalG3.pem.inc"
    IF_ENABLED(CONFIG_TLS_CREDENTIALS, (0x00))};

static void lte_event_handler(const struct lte_lc_evt *const evt) {
  switch (evt->type) {
  case LTE_LC_EVT_NW_REG_STATUS:
    if (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME ||
        evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING) {
      LOG_INF("LTE connected");
      k_sem_give(&lte_connected_sem);
    }
    break;
  case LTE_LC_EVT_PSM_UPDATE:
    // LOG_INF("PSM: TAU=%d, active=%d",
    //     evt->psm_cfg.tau, evt->psm_cfg.active_time);
    break;
  default:
    break;
  }
}

int cellular_init(void) {
  nrf_modem_lib_init();
  int err;
  bool exists;

  LOG_INF("Provisioning certificate");

  err = modem_key_mgmt_exists(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
                              &exists);
  if (err) {
    LOG_ERR("modem_key_mgmt_exists failed: %d", err);
    return err;
  }

  if (exists) {
    int mismatch = modem_key_mgmt_cmp(
        TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert, sizeof(cert));
    if (!mismatch) {
      LOG_INF("Certificate match");
      return 0;
    }
    LOG_INF("Certificate mismatch, updating");
    modem_key_mgmt_delete(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
  }

  err = modem_key_mgmt_write(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN,
                             cert, sizeof(cert));
  if (err) {
    LOG_ERR("Certificate write failed: %d", err);
    return err;
  }

  LOG_INF("Certificate provisioned");
  return 0;
}

int cellular_connect(void) {
  int err;

  LOG_INF("Connecting to LTE");

  // Set APN for Hologram SIM
  nrf_modem_at_printf("AT+CGDCONT=1,\"IP\",\"hologram\"");
  nrf_modem_at_printf("AT+COPS=0,2"); /* auto select, numeric format */

  // Force T-Mobile network for Hologram SIM if failing to connect
  // nrf_modem_at_printf("AT+COPS=1,2,\"310260\",7");

  k_sleep(K_SECONDS(5));
  lte_lc_register_handler(lte_event_handler);

  /* Request PSM: 6 min TAU, 2s active time */
  err = lte_lc_psm_req(true);
  if (err) {
    LOG_WRN("PSM request failed: %d", err);
  }

  err = lte_lc_connect_async(lte_event_handler);
  if (err) {
    LOG_ERR("lte_lc_init_and_connect_async failed: %d", err);
    return err;
  }

  /* Wait up to 60 seconds for connection */
  err = k_sem_take(&lte_connected_sem, K_SECONDS(180));
  if (err) {
    LOG_ERR("LTE connection timeout");
    return -ETIMEDOUT;
  }

  return 0;
}

int cellular_disconnect(void) {
  LOG_INF("Disconnecting LTE");
  // lte_lc_power_off(); graceful shutdown to avoid 30 minute lockout
  lte_lc_offline();
  k_sleep(K_SECONDS(1));
  lte_lc_power_off();
  k_sem_reset(&lte_connected_sem);
  return 0;
}

static int tls_setup(int fd) {
  int err;
  int verify = 2;
  const sec_tag_t tls_sec_tag[] = {TLS_SEC_TAG};

  err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
  if (err)
    return err;

  err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag,
                   sizeof(tls_sec_tag));
  if (err)
    return err;

  err = setsockopt(fd, SOL_TLS, TLS_HOSTNAME, WEBHOOK_HOST,
                   sizeof(WEBHOOK_HOST) - 1);
  return err;
}

int cellular_post_frames(const radar_buf_t *buf) {
  int err, fd, bytes;
  size_t off;
  struct addrinfo *res;
  struct addrinfo hints = {
      .ai_flags = AI_NUMERICSERV,
      .ai_socktype = SOCK_STREAM,
  };
  char peer_addr[INET6_ADDRSTRLEN];
  char body[2048];
  char header[512];
  int body_len;

  /* Build JSON body with all frames */
  body_len = snprintf(body, sizeof(body), "{\"frames\":[");
  for (int i = 0; i < buf->count; i++) {
    const radar_frame_t *f = &buf->frames[i];
    body_len += snprintf(body + body_len, sizeof(body) - body_len,
                         "%s{\"t\":%u,\"n\":%u,\"peaks\":[", i > 0 ? "," : "",
                         f->timestamp_ms, (unsigned int)f->num_peaks);
    for (int p = 0; p < f->num_peaks; p++) {
      body_len += snprintf(body + body_len, sizeof(body) - body_len,
                           "%s{\"d\":%.4f,\"s\":%.2f}", p > 0 ? "," : "",
                           (double)f->peaks[p].distance_m,
                           (double)f->peaks[p].strength);
    }
    body_len += snprintf(body + body_len, sizeof(body) - body_len, "]}");
  }
  body_len += snprintf(body + body_len, sizeof(body) - body_len, "]}");

  /* Build HTTP header */
  snprintf(header, sizeof(header),
           "POST /" WEBHOOK_UUID " HTTP/1.1\r\n"
           "Host: " WEBHOOK_HOST "\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: %d\r\n"
           "Connection: close\r\n\r\n",
           body_len);

  LOG_INF("Resolving %s", WEBHOOK_HOST);
  err = getaddrinfo(WEBHOOK_HOST, HTTPS_PORT, &hints, &res);
  if (err) {
    LOG_ERR("getaddrinfo failed: %d", err);
    return -EIO;
  }

  inet_ntop(res->ai_family, &((struct sockaddr_in *)(res->ai_addr))->sin_addr,
            peer_addr, INET6_ADDRSTRLEN);
  LOG_INF("Resolved %s", peer_addr);

  fd = socket(res->ai_family, SOCK_STREAM, IPPROTO_TLS_1_2);
  if (fd < 0) {
    LOG_ERR("socket() failed");
    freeaddrinfo(res);
    return -EIO;
  }

  err = tls_setup(fd);
  if (err) {
    LOG_ERR("tls_setup failed: %d", err);
    goto cleanup;
  }

  err = connect(fd, res->ai_addr, res->ai_addrlen);
  if (err) {
    LOG_ERR("connect() failed: %d", errno);
    goto cleanup;
  }

  /* Send header */
  off = 0;
  size_t hlen = strlen(header);
  do {
    bytes = send(fd, header + off, hlen - off, 0);
    if (bytes < 0) {
      err = -EIO;
      goto cleanup;
    }
    off += bytes;
  } while (off < hlen);

  /* Send body */
  off = 0;
  do {
    bytes = send(fd, body + off, body_len - off, 0);
    if (bytes < 0) {
      err = -EIO;
      goto cleanup;
    }
    off += bytes;
  } while ((int)off < body_len);

  LOG_INF("Sent %d bytes total", (int)(hlen + body_len));

  /* Receive response */
  off = 0;
  do {
    bytes = recv(fd, recv_buf + off, RECV_BUF_SIZE - off - 1, 0);
    if (bytes < 0) {
      err = -EIO;
      goto cleanup;
    }
    off += bytes;
  } while (bytes != 0);

  recv_buf[off] = '\0';
  LOG_INF("Response: %.100s", recv_buf);
  err = 0;

cleanup:
  freeaddrinfo(res);
  close(fd);
  return err;
}
