/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <stdio.h>

#include "../include/nfc.h"

/*
 * Test for the generic NFC SPI driver ("SPI").
 * poll() should return 1 (no tag) for the generic implementation.
 */
int main(void)
{
    struct nfc_dev *dev = nfc_alloc_spi("SPI:nfc_spi", "/dev/spidev0.0", 0);
    if (!dev) {
        fprintf(stderr, "alloc spi failed (generic driver not linked?)\n");
        return 1;
    }

    if (nfc_init(dev) < 0) {
        fprintf(stderr, "init failed\n");
        nfc_free(dev);
        return 1;
    }

    struct nfc_tag_info info;
    int r = nfc_poll(dev, &info, 10);
    if (r != 1) {
        fprintf(stderr, "expected 1(no tag), got %d\n", r);
        nfc_free(dev);
        return 1;
    }

    int rb = nfc_read_block(dev, 0, (uint8_t *)&info, 4);
    if (rb != -ENOSYS) {
        fprintf(stderr, "expected -ENOSYS, got %d\n", rb);
        nfc_free(dev);
        return 1;
    }

    nfc_free(dev);
    return 0;
}

