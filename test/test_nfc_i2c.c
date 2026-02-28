/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "../include/nfc.h"

static void on_tag_event(struct nfc_dev *dev, const struct nfc_tag_info *info, void *ctx)
{
    (void)ctx;
    printf("[callback] dev=%p uid_len=%u type=%d rssi=%d\n",
        (void *)dev,
        info ? info->uid_len : 0,
        info ? info->type : -1,
        info ? info->rssi_dbm : 0);
}

static void print_uid(const struct nfc_tag_info *info)
{
    printf("  UID: ");
    for (uint8_t i = 0; i < info->uid_len; i++) {
        printf("%02X ", info->uid[i]);
    }
    printf("\n");
}

int main(int argc, char **argv)
{
    const char *dev_path = "/dev/i2c-5";
    uint8_t addr = 0x28;
    uint8_t block = 4;

    if (argc > 1) dev_path = argv[1];
    if (argc > 2) addr = (uint8_t)strtoul(argv[2], NULL, 0);
    if (argc > 3) block = (uint8_t)strtoul(argv[3], NULL, 0);

    printf("Using: dev=%s addr=0x%02X block=%u\n", dev_path, addr, block);

    struct nfc_dev *dev = nfc_alloc_i2c("SI512:nfc0", dev_path, addr);
    if (!dev) {
        fprintf(stderr, "alloc i2c failed\n");
        return -1;
    }

    nfc_set_callback(dev, on_tag_event, NULL);

    if (nfc_init(dev) < 0) {
        fprintf(stderr, "init failed\n");
        nfc_free(dev);
        return -1;
    }

    for (int i = 0; i < 20; i++) {
        struct nfc_tag_info info;
        int ret = nfc_poll(dev, &info, 100);
        if (ret == 0) {
            printf("[poll] tag detected:\n");
            print_uid(&info);

            // 演示读写接口（I2C 设备未实现时通常会返回错误）
            uint8_t buf[16] = {0};
            int rlen = nfc_read_block(dev, block, buf, sizeof(buf));
            printf("read block ret=%d first4=%02X %02X %02X %02X\n", rlen, buf[0], buf[1], buf[2], buf[3]);

            uint8_t wbuf[16] = {0x11, 0x22, 0x33, 0x44};
            int wlen = nfc_write_block(dev, block, wbuf, sizeof(wbuf));
            printf("write block ret=%d\n", wlen);

            nfc_free(dev);

            break;
        } else if (ret == 1) {
            printf("[poll] no tag\n");
        } else {
            printf("[poll] error=%d\n", ret);
        }
        usleep(200000);
    }


    return 0;
}
