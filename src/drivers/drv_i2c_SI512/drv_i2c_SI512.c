/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "drv_i2c_SI512.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nfc_core.h>

#ifndef I2C_SLAVE
#define I2C_SLAVE 0x0703
#endif

/* ---------- logging (keep quiet by default) ---------- */
/* #define SI512_DEBUG */

#ifdef SI512_DEBUG
#define si512_log(...) do { \
    printf("[SI512] %s:%d ", __func__, __LINE__); \
    printf(__VA_ARGS__); \
} while (0)
#else
#define si512_log(...) do { } while (0)
#endif

/* ---------- cross-process lock helpers ---------- */
#ifndef NFC_LOCK_DIR
#define NFC_LOCK_DIR "/var/lock"
#endif

static void make_lock_path(char *out, size_t out_sz, const char *dev_path, uint8_t addr)
{
    char dev_sanitized[96];
    size_t wi = 0;

    for (size_t i = 0; dev_path && dev_path[i] && wi + 1 < sizeof(dev_sanitized); i++) {
        char c = dev_path[i];
        if (c == '/')
            c = '_';
        dev_sanitized[wi++] = c;
    }
    dev_sanitized[wi] = '\0';

    snprintf(out, out_sz, "%s/nfc_i2c%s_addr_0x%02X.lock",
        NFC_LOCK_DIR, dev_sanitized, (unsigned)addr);
}

static uint64_t now_ms_monotonic(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

/* Try to take the bus lock for a bounded time (ms). Returns 0 on success, -1 on timeout/error. */
static int lock_with_timeout_ms(int lock_fd, uint32_t wait_ms)
{
    uint64_t start = now_ms_monotonic();
    for (;;) {
        if (flock(lock_fd, LOCK_EX | LOCK_NB) == 0)
            return 0;

        if (errno != EWOULDBLOCK && errno != EAGAIN)
            return -1;

        if (wait_ms == 0)
            return -1;

        if (now_ms_monotonic() - start >= (uint64_t)wait_ms)
            return -1;

        usleep(2 * 1000);
    }
}

static void unlock_noexcept(int lock_fd)
{
    if (lock_fd >= 0)
        (void)flock(lock_fd, LOCK_UN);
}

/* ---------- private driver state ---------- */
struct si512_i2c_priv {
    char dev_path[64];
    uint8_t addr;
    int fd;

    int lock_fd;
    char lock_path[160];
};

/* ---------- low-level I2C register access ---------- */

static int write_to_i2c(int fd, uint8_t reg, uint8_t value)
{
    uint8_t buffer[2];
    buffer[0] = reg;
    buffer[1] = value;
    ssize_t wr = write(fd, buffer, 2);
    return (wr == 2) ? 0 : -1;
}

static int read_from_i2c(int fd, uint8_t reg, uint8_t *data, size_t len)
{
    ssize_t wr = write(fd, &reg, 1);
    if (wr != 1)
        return -1;
    ssize_t rd = read(fd, data, len);
    return (rd == (ssize_t)len) ? 0 : -1;
}

static void I_SI512_IO_Write(int fd, unsigned char RegAddr, unsigned char value)
{
    if (write_to_i2c(fd, (uint8_t)RegAddr, (uint8_t)value) != 0)
        si512_log("write reg 0x%02X fail\n", (unsigned)RegAddr);
}

static unsigned char I_SI512_IO_Read(int fd, unsigned char RegAddr)
{
    uint8_t v = 0;
    if (read_from_i2c(fd, (uint8_t)RegAddr, &v, 1) != 0)
        si512_log("read reg 0x%02X fail\n", (unsigned)RegAddr);
    return (unsigned char)v;
}

/* ---------- SI512/MFRC522-like helpers ---------- */

void I_SI512_ClearBitMask(int fd, unsigned char reg, unsigned char mask)
{
    unsigned char tmp = I_SI512_IO_Read(fd, reg);
    I_SI512_IO_Write(fd, reg, (unsigned char)(tmp & (unsigned char)(~mask)));
}

void I_SI512_SetBitMask(int fd, unsigned char reg, unsigned char mask)
{
    unsigned char tmp = I_SI512_IO_Read(fd, reg);
    I_SI512_IO_Write(fd, reg, (unsigned char)(tmp | mask));
}

void PcdAntennaOn(int fd)
{
    unsigned char i = I_SI512_IO_Read(fd, TxControlReg);
    if (!(i & 0x03))
        I_SI512_SetBitMask(fd, TxControlReg, 0x03);
}

void PcdAntennaOff(int fd)
{
    I_SI512_ClearBitMask(fd, TxControlReg, 0x03);
}

void CalulateCRC(int fd, unsigned char *pIndata, unsigned char len, unsigned char *pOutData)
{
    unsigned char i, n;

    I_SI512_ClearBitMask(fd, DivIrqReg, 0x04);
    I_SI512_IO_Write(fd, CommandReg, PCD_IDLE);
    I_SI512_SetBitMask(fd, FIFOLevelReg, 0x80);

    for (i = 0; i < len; i++)
        I_SI512_IO_Write(fd, FIFODataReg, *(pIndata + i));

    I_SI512_IO_Write(fd, CommandReg, PCD_CALCCRC);

    i = 0xFF;
    do {
        n = I_SI512_IO_Read(fd, DivIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x04));

    pOutData[0] = I_SI512_IO_Read(fd, CRCResultRegL);
    pOutData[1] = I_SI512_IO_Read(fd, CRCResultRegH);
}

char PcdComMF522(int fd,
        unsigned char Command,
        unsigned char *pInData,
        unsigned char InLenByte,
        unsigned char *pOutData,
        unsigned int *pOutLenBit)
{
    char status = MI_ERR;
    unsigned char irqEn = 0x00;
    unsigned char waitFor = 0x00;
    unsigned char lastBits;
    unsigned char n;
    unsigned int i;

    switch (Command) {
    case PCD_AUTHENT:
        irqEn = 0x12;
        waitFor = 0x10;
        break;
    case PCD_TRANSCEIVE:
        irqEn = 0x77;
        waitFor = 0x30;
        break;
    default:
        break;
    }

    I_SI512_ClearBitMask(fd, ComIrqReg, 0x80);
    I_SI512_IO_Write(fd, CommandReg, PCD_IDLE);
    I_SI512_SetBitMask(fd, FIFOLevelReg, 0x80);

    for (i = 0; i < InLenByte; i++)
        I_SI512_IO_Write(fd, FIFODataReg, pInData[i]);

    I_SI512_IO_Write(fd, CommandReg, Command);

    if (Command == PCD_TRANSCEIVE)
        I_SI512_SetBitMask(fd, BitFramingReg, 0x80);

    i = 2000;
    do {
        n = I_SI512_IO_Read(fd, ComIrqReg);
        i--;
    } while ((i != 0) && !(n & 0x01) && !(n & waitFor));

    I_SI512_ClearBitMask(fd, BitFramingReg, 0x80);

    if (i != 0) {
        if (!(I_SI512_IO_Read(fd, ErrorReg) & 0x1B)) {
            status = MI_OK;
            if (n & irqEn & 0x01)
                status = MI_NOTAGERR;

            if (Command == PCD_TRANSCEIVE) {
                n = I_SI512_IO_Read(fd, FIFOLevelReg);
                lastBits = I_SI512_IO_Read(fd, ControlReg) & 0x07;

                if (lastBits)
                    *pOutLenBit = (n - 1) * 8 + lastBits;
                else
                    *pOutLenBit = n * 8;

                if (n == 0)
                    n = 1;
                if (n > MAXRLEN)
                    n = MAXRLEN;

                for (i = 0; i < n; i++)
                    pOutData[i] = I_SI512_IO_Read(fd, FIFODataReg);
            }
        } else {
            status = MI_ERR;
        }
    }

    I_SI512_SetBitMask(fd, ControlReg, 0x80);
    I_SI512_IO_Write(fd, CommandReg, PCD_IDLE);
    return status;
}

char PcdRequest(int fd, unsigned char req_code, unsigned char *pTagType)
{
    char status;
    unsigned int unLen;
    unsigned char buf[MAXRLEN];

    I_SI512_ClearBitMask(fd, Status2Reg, 0x08);
    I_SI512_IO_Write(fd, BitFramingReg, 0x07);
    I_SI512_SetBitMask(fd, TxControlReg, 0x03);

    buf[0] = req_code;
    status = PcdComMF522(fd, PCD_TRANSCEIVE, buf, 1, buf, &unLen);
    if ((status == MI_OK) && (unLen == 0x10)) {
        pTagType[0] = buf[0];
        pTagType[1] = buf[1];
        return MI_OK;
    }
    return MI_ERR;
}

char PcdAnticoll(int fd, unsigned char *pSnr, unsigned char anticollision_level)
{
    char status;
    unsigned char i, snr_check = 0;
    unsigned int unLen;
    unsigned char buf[MAXRLEN];

    I_SI512_ClearBitMask(fd, Status2Reg, 0x08);
    I_SI512_IO_Write(fd, BitFramingReg, 0x00);
    I_SI512_ClearBitMask(fd, CollReg, 0x80);

    buf[0] = anticollision_level;
    buf[1] = 0x20;

    status = PcdComMF522(fd, PCD_TRANSCEIVE, buf, 2, buf, &unLen);
    if (status == MI_OK) {
        for (i = 0; i < 4; i++) {
            pSnr[i] = buf[i];
            snr_check ^= buf[i];
        }
        if (snr_check != buf[i])
            status = MI_ERR;
    }

    I_SI512_SetBitMask(fd, CollReg, 0x80);
    return status;
}

static char PcdSelect_common(int fd, unsigned char sel_cmd, unsigned char *pSnr, unsigned char *sak)
{
    char status;
    unsigned char i;
    unsigned int unLen;
    unsigned char buf[MAXRLEN];

    buf[0] = sel_cmd;
    buf[1] = 0x70;
    buf[6] = 0;

    for (i = 0; i < 4; i++) {
        buf[i + 2] = pSnr[i];
        buf[6] ^= pSnr[i];
    }

    CalulateCRC(fd, buf, 7, &buf[7]);

    I_SI512_ClearBitMask(fd, Status2Reg, 0x08);
    status = PcdComMF522(fd, PCD_TRANSCEIVE, buf, 9, buf, &unLen);

    if ((status == MI_OK) && (unLen == 0x18)) {
        *sak = buf[0];
        return MI_OK;
    }
    return MI_ERR;
}

char PcdSelect1(int fd, unsigned char *pSnr, unsigned char *sak)
{
    return PcdSelect_common(fd, PICC_ANTICOLL1, pSnr, sak);
}

char PcdSelect2(int fd, unsigned char *pSnr, unsigned char *sak)
{
    return PcdSelect_common(fd, PICC_ANTICOLL2, pSnr, sak);
}

char PcdSelect3(int fd, unsigned char *pSnr, unsigned char *sak)
{
    return PcdSelect_common(fd, PICC_ANTICOLL3, pSnr, sak);
}

char PcdHalt(int fd)
{
    char status;
    unsigned int unLen;
    unsigned char buf[MAXRLEN];

    buf[0] = PICC_HALT;
    buf[1] = 0x00;
    CalulateCRC(fd, buf, 2, &buf[2]);

    status = PcdComMF522(fd, PCD_TRANSCEIVE, buf, 4, buf, &unLen);
    return status;
}

char PcdRead(int fd, unsigned char addr, unsigned char *pData)
{
    char status;
    unsigned int unLen;
    unsigned char buf[MAXRLEN];

    buf[0] = PICC_READ;
    buf[1] = addr;
    CalulateCRC(fd, buf, 2, &buf[2]);

    status = PcdComMF522(fd, PCD_TRANSCEIVE, buf, 4, buf, &unLen);
    if ((status == MI_OK) && (unLen == 0x90)) {
        memcpy(pData, buf, 16);
        return MI_OK;
    }
    return MI_ERR;
}

char PcdWrite(int fd, unsigned char addr, unsigned char *pData)
{
    char status;
    unsigned int unLen;
    unsigned char buf[MAXRLEN];

    buf[0] = PICC_WRITE;
    buf[1] = addr;
    CalulateCRC(fd, buf, 2, &buf[2]);

    status = PcdComMF522(fd, PCD_TRANSCEIVE, buf, 4, buf, &unLen);
    if ((status != MI_OK) || (unLen != 4) || ((buf[0] & 0x0F) != 0x0A))
        return MI_ERR;

    memcpy(buf, pData, 16);
    CalulateCRC(fd, buf, 16, &buf[16]);

    status = PcdComMF522(fd, PCD_TRANSCEIVE, buf, 18, buf, &unLen);
    if ((status != MI_OK) || (unLen != 4) || ((buf[0] & 0x0F) != 0x0A))
        return MI_ERR;

    return MI_OK;
}

void PCD_SI512_TypeA_Init(int fd)
{
    I_SI512_IO_Write(fd, ControlReg, 0x10);

    I_SI512_ClearBitMask(fd, Status2Reg, 0x08);

    I_SI512_IO_Write(fd, TxModeReg, 0x00);
    I_SI512_IO_Write(fd, RxModeReg, 0x00);
    I_SI512_IO_Write(fd, ModWidthReg, 0x26);

    I_SI512_IO_Write(fd, RFCfgReg, RFCfgReg_Val);

    I_SI512_IO_Write(fd, TModeReg, 0x80);
    I_SI512_IO_Write(fd, TPrescalerReg, 0xa9);
    I_SI512_IO_Write(fd, TReloadRegH, 0x03);
    I_SI512_IO_Write(fd, TReloadRegL, 0xe8);

    I_SI512_IO_Write(fd, TxASKReg, 0x40);
    I_SI512_IO_Write(fd, ModeReg, 0x3D);
    I_SI512_IO_Write(fd, CommandReg, 0x00);

    PcdAntennaOn(fd);
}

int PCD_SI512_TypeA_GetUID(int fd, unsigned char *uid, unsigned char *data)
{
    unsigned char ATQA[2];
    unsigned char UID[12];
    unsigned char SAK = 0;

    unsigned char CardReadBuf[16];
    memset(UID, 0, sizeof(UID));
    memset(CardReadBuf, 0, sizeof(CardReadBuf));

    I_SI512_IO_Write(fd, RFCfgReg, RFCfgReg_Val);

    if (PcdRequest(fd, PICC_REQIDL, ATQA) != MI_OK) {
        I_SI512_IO_Write(fd, RFCfgReg, 0x48);
        if (PcdRequest(fd, PICC_REQIDL, ATQA) != MI_OK) {
            I_SI512_IO_Write(fd, RFCfgReg, 0x58);
            if (PcdRequest(fd, PICC_REQIDL, ATQA) != MI_OK)
                return 1;
        }
    }

    if (PcdAnticoll(fd, UID, PICC_ANTICOLL1) != MI_OK)
        return 1;
    if (PcdSelect1(fd, UID, &SAK) != MI_OK)
        return 1;

    if (SAK & 0x04) {
        if (PcdAnticoll(fd, UID + 4, PICC_ANTICOLL2) != MI_OK)
            return 1;
        if (PcdSelect2(fd, UID + 4, &SAK) != MI_OK)
            return 1;

        if (SAK & 0x04) {
            if (PcdAnticoll(fd, UID + 8, PICC_ANTICOLL3) != MI_OK)
                return 1;
            if (PcdSelect3(fd, UID + 8, &SAK) != MI_OK)
                return 1;
        }
    }

    if (uid)
        memcpy(uid, UID, 12);

    if (data) {
        unsigned char out[16];
        memset(out, 0, sizeof(out));
        if (PcdRead(fd, 0x07, CardReadBuf) == MI_OK) {
            int wi = 0;
            for (int i = 0; i < 16; i++) {
                if (CardReadBuf[i] == 0xfe)
                    break;
                if (CardReadBuf[i] == 0x65 || CardReadBuf[i] == 0x6e)
                    continue;
                if (wi < (int)sizeof(out))
                    out[wi++] = CardReadBuf[i];
            }
            memcpy(data, out, 16);
        }
    }

    return 0;
}

void PcdReset(int fd)
{
    I_SI512_IO_Write(fd, CommandReg, 0x0f);
    while (I_SI512_IO_Read(fd, CommandReg) & 0x10) {
        /* busy wait */
    }
    usleep(100);
}

void PcdPowerdown(void)
{
    /* no-op */
}

/* ---------- nfc_ops implementation ---------- */

static int si512_init(struct nfc_dev *dev)
{
    struct si512_i2c_priv *priv = dev->priv_data;

    printf("[NFC-SI512] init: %s dev=%s addr=0x%02X\n",
        dev->name, priv->dev_path, priv->addr);

    priv->fd = -1;
    priv->lock_fd = -1;
    priv->lock_path[0] = '\0';

    make_lock_path(priv->lock_path, sizeof(priv->lock_path), priv->dev_path, priv->addr);
    priv->lock_fd = open(priv->lock_path, O_CREAT | O_RDWR | O_CLOEXEC, 0664);
    if (priv->lock_fd < 0) {
        printf("[NFC-SI512] warn: open lock(%s) failed (%s)\n",
            priv->lock_path, strerror(errno));
    }

    int fd = open(priv->dev_path, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        printf("[NFC-SI512] warn: open(%s) failed (%s)\n", priv->dev_path, strerror(errno));
        return 0;
    }

    if (ioctl(fd, I2C_SLAVE, (int)priv->addr) < 0) {
        printf("[NFC-SI512] warn: ioctl(I2C_SLAVE) failed (%s)\n", strerror(errno));
        close(fd);
        return 0;
    }

    priv->fd = fd;

    /* Init hardware under lock once */
    if (priv->lock_fd >= 0 && lock_with_timeout_ms(priv->lock_fd, 200) == 0) {
        PCD_SI512_TypeA_Init(priv->fd);
        unlock_noexcept(priv->lock_fd);
    } else {
        PCD_SI512_TypeA_Init(priv->fd);
    }

    return 0;
}

static int si512_poll(struct nfc_dev *dev, struct nfc_tag_info *info, uint32_t timeout_ms)
{
    struct si512_i2c_priv *priv = dev->priv_data;
    unsigned char uid12[12];
    unsigned char data16[16];

    if (!priv || priv->fd < 0)
        return 1;

    uint64_t start = now_ms_monotonic();
    uint64_t deadline = start + (uint64_t)timeout_ms;
    if (timeout_ms == 0)
        deadline = start;

    for (;;) {
        /* lock only around one I2C transaction, then release so N processes can round-robin */
        if (priv->lock_fd >= 0) {
            if (lock_with_timeout_ms(priv->lock_fd, 50) != 0) {
                /* couldn't get bus quickly; treat as "no tag yet" and retry until overall timeout */
                goto poll_check_timeout;
            }
        }

        memset(uid12, 0, sizeof(uid12));
        memset(data16, 0, sizeof(data16));
        int r = PCD_SI512_TypeA_GetUID(priv->fd, uid12, data16);

        if (priv->lock_fd >= 0)
            unlock_noexcept(priv->lock_fd);

        if (r == 0) {
            uint8_t uid_len = 4;
            if (uid12[4] || uid12[5] || uid12[6] || uid12[7])
                uid_len = 8;
            if (uid12[8] || uid12[9] || uid12[10] || uid12[11])
                uid_len = 12;

            memset(info, 0, sizeof(*info));
            memcpy(info->uid, uid12, uid_len);
            info->uid_len = uid_len;
            info->type = NFC_TAG_MIFARE_CLASSIC;
            info->rssi_dbm = 0;
            info->ats_len = 0;

            if (dev->cb)
                dev->cb(dev, info, dev->cb_ctx);

            return 0;
        }

poll_check_timeout:
        if (timeout_ms == 0 || now_ms_monotonic() >= deadline)
            return 1;

        /* backoff a bit so multiple processes share the bus fairly */
        usleep(10 * 1000);
    }
}

static int si512_read_block(struct nfc_dev *dev, uint8_t block, uint8_t *buf, size_t len)
{
    struct si512_i2c_priv *priv = dev->priv_data;

    if (!priv || priv->fd < 0)
        return -ENODEV;
    if (!buf)
        return -EINVAL;
    if (len != 16)
        return -EINVAL;

    if (priv->lock_fd >= 0 && lock_with_timeout_ms(priv->lock_fd, 500) != 0)
        return -EBUSY;

    unsigned char tmp[16];
    int ok = (PcdRead(priv->fd, block, tmp) == MI_OK);

    if (priv->lock_fd >= 0)
        unlock_noexcept(priv->lock_fd);

    if (!ok)
        return -EIO;

    memcpy(buf, tmp, 16);
    return 16;
}

static int si512_write_block(struct nfc_dev *dev, uint8_t block, const uint8_t *buf, size_t len)
{
    struct si512_i2c_priv *priv = dev->priv_data;

    if (!priv || priv->fd < 0)
        return -ENODEV;
    if (!buf)
        return -EINVAL;
    if (len != 16)
        return -EINVAL;

    if (priv->lock_fd >= 0 && lock_with_timeout_ms(priv->lock_fd, 500) != 0)
        return -EBUSY;

    unsigned char tmp[16];
    memcpy(tmp, buf, 16);

    int ok = (PcdWrite(priv->fd, block, tmp) == MI_OK);

    if (priv->lock_fd >= 0)
        unlock_noexcept(priv->lock_fd);

    if (!ok)
        return -EIO;

    return 16;
}

static void si512_free(struct nfc_dev *dev)
{
    if (!dev)
        return;

    printf("[NFC-SI512] free: %s\n", dev->name);

    {
        struct si512_i2c_priv *priv = dev->priv_data;
        if (priv && priv->fd >= 0) {
            close(priv->fd);
            priv->fd = -1;
        }
        if (priv && priv->lock_fd >= 0) {
            close(priv->lock_fd);
            priv->lock_fd = -1;
        }
    }

    if (dev->last_tag) free(dev->last_tag);
    if (dev->priv_data) free(dev->priv_data);
    if (dev->name) free((void *)dev->name);
    free(dev);
}

static const struct nfc_ops si512_ops = {
    .init = si512_init,
    .poll = si512_poll,
    .read_block = si512_read_block,
    .write_block = si512_write_block,
    .free = si512_free,
};

static struct nfc_dev *si512_create(void *args)
{
    struct nfc_args_i2c *a = (struct nfc_args_i2c *)args;
    struct nfc_dev *dev;
    struct si512_i2c_priv *priv;

    if (!a || !a->instance || !a->dev_path)
        return NULL;

    dev = nfc_dev_alloc(a->instance, sizeof(*priv));
    if (!dev)
        return NULL;

    dev->ops = &si512_ops;
    priv = dev->priv_data;

    strncpy(priv->dev_path, a->dev_path, sizeof(priv->dev_path) - 1);
    priv->dev_path[sizeof(priv->dev_path) - 1] = '\0';
    priv->addr = a->addr;
    priv->fd = -1;
    priv->lock_fd = -1;
    priv->lock_path[0] = '\0';

    return dev;
}

REGISTER_NFC_DRIVER("SI512", NFC_DRV_I2C, si512_create);
