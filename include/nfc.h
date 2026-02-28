/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef NFC_H
#define NFC_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NFC tag types
 */
enum nfc_tag_type {
    NFC_TAG_UNKNOWN = 0,
    NFC_TAG_MIFARE_CLASSIC,
    NFC_TAG_MIFARE_ULTRALIGHT,
    NFC_TAG_TYPE_A,
    NFC_TAG_TYPE_B,
    NFC_TAG_FELICA,
    NFC_TAG_ISO15693,
};

/*
 * struct nfc_tag_info - tag information
 * @uid:      unique identifier
 * @uid_len:  uid length in bytes
 * @type:     tag type
 * @rssi_dbm: signal strength (optional)
 * @ats:      answer to select (optional)
 * @ats_len:  ats length
 */
struct nfc_tag_info {
    uint8_t uid[16];
    uint8_t uid_len;
    enum nfc_tag_type type;
    int8_t  rssi_dbm;
    uint8_t ats[32];
    uint8_t ats_len;
};

/* opaque handle */
struct nfc_dev;

typedef void (*nfc_event_cb_t)(struct nfc_dev *dev,
    const struct nfc_tag_info *info, void *ctx);

/* --- lifecycle --- */

int nfc_init(struct nfc_dev *dev);
void nfc_set_callback(struct nfc_dev *dev, nfc_event_cb_t cb, void *ctx);
void nfc_free(struct nfc_dev *dev);

/* --- operations --- */

int nfc_poll(struct nfc_dev *dev, struct nfc_tag_info *info,
    uint32_t timeout_ms);
int nfc_read_block(struct nfc_dev *dev, uint8_t block, uint8_t *buf,
    size_t len);
int nfc_write_block(struct nfc_dev *dev, uint8_t block, const uint8_t *buf,
    size_t len);

/* --- factory functions --- */

struct nfc_dev *nfc_alloc_i2c(const char *name, const char *i2c_dev,
    uint8_t addr);
struct nfc_dev *nfc_alloc_spi(const char *name, const char *spi_dev,
    uint32_t cs_pin);
struct nfc_dev *nfc_alloc_uart(const char *name, const char *uart_dev,
    uint32_t baud);


#ifdef __cplusplus
}
#endif

#endif /* NFC_H */
