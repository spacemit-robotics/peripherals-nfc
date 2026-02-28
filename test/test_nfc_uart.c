/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <stdio.h>

#include "../include/nfc.h"

/*
 * Test for the generic NFC UART driver ("UART").
 */
int main(void)
{
    struct nfc_dev *dev = nfc_alloc_uart("UART:nfc_uart", "/dev/ttyS0", 115200);
    if (!dev) {
        fprintf(stderr, "alloc uart failed (generic driver not linked?)\n");
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

    nfc_free(dev);
    return 0;
}

