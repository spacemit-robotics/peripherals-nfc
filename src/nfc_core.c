/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include "nfc_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>

struct nfc_dev *nfc_dev_alloc(const char *name, size_t priv_size)
{
    struct nfc_dev *dev;
    void *priv = NULL;
    char *name_copy = NULL;

    dev = calloc(1, sizeof(*dev));
    if (!dev)
        return NULL;

    if (priv_size) {
        priv = calloc(1, priv_size);
        if (!priv) {
            free(dev);
            return NULL;
        }
        dev->priv_data = priv;
    }

    if (name) {
        size_t n = strlen(name);
        name_copy = calloc(1, n + 1);
        if (!name_copy) {
            free(priv);
            free(dev);
            return NULL;
        }
        memcpy(name_copy, name, n);
        name_copy[n] = '\0';
        dev->name = name_copy;
    }

    return dev;
}

int nfc_init(struct nfc_dev *dev)
{
    if (!dev || !dev->ops || !dev->ops->init)
        return -1;

    return dev->ops->init(dev);
}

void nfc_set_callback(struct nfc_dev *dev, nfc_event_cb_t cb, void *ctx)
{
    if (dev) {
        dev->cb = cb;
        dev->cb_ctx = ctx;
    }
}

int nfc_poll(struct nfc_dev *dev, struct nfc_tag_info *info, uint32_t timeout_ms)
{
    if (!dev || !dev->ops || !dev->ops->poll || !info)
        return -1;

    return dev->ops->poll(dev, info, timeout_ms);
}

int nfc_read_block(struct nfc_dev *dev, uint8_t block, uint8_t *buf, size_t len)
{
    if (!dev || !dev->ops || !dev->ops->read_block || !buf)
        return -1;

    return dev->ops->read_block(dev, block, buf, len);
}

int nfc_write_block(struct nfc_dev *dev, uint8_t block, const uint8_t *buf, size_t len)
{
    if (!dev || !dev->ops || !dev->ops->write_block || !buf)
        return -1;

    return dev->ops->write_block(dev, block, buf, len);
}

void nfc_free(struct nfc_dev *dev)
{
    if (!dev)
        return;

    if (dev->ops && dev->ops->free) {
        dev->ops->free(dev);
        return;
    }

    if (dev->last_tag) free(dev->last_tag);
    if (dev->priv_data) free(dev->priv_data);
    if (dev->name) free((void *)dev->name);
    free(dev);
}

/* --- driver registry (minimal, motor-like) --- */

static struct driver_info *g_driver_list = NULL;

void nfc_driver_register(struct driver_info *info)
{
    if (!info)
        return;
    info->next = g_driver_list;
    g_driver_list = info;
}

static struct driver_info *find_driver(const char *name, enum nfc_driver_type type)
{
    struct driver_info *curr = g_driver_list;
    while (curr) {
        if (curr->name && name && strcmp(curr->name, name) == 0) {
            if (curr->type == type)
                return curr;
            printf("[NFC] driver '%s' type mismatch (expected %d got %d)\n",
                name, (int)type, (int)curr->type);
            return NULL;
        }
        curr = curr->next;
    }
    printf("[NFC] driver '%s' not found\n", name ? name : "(null)");
    return NULL;
}

static int split_driver_instance(const char *name,
    char *driver, size_t driver_sz,
    const char **instance)
{
    const char *sep;
    size_t len;

    if (!name || !driver || !driver_sz || !instance)
        return -1;

    sep = strchr(name, ':');
    if (!sep)
        return 0;

    len = (size_t)(sep - name);
    if (len == 0 || len + 1 > driver_sz || !*(sep + 1))
        return -1;

    memcpy(driver, name, len);
    driver[len] = '\0';
    *instance = sep + 1;
    return 1;
}

/* --- factory functions (public API) --- */

struct nfc_dev *nfc_alloc_i2c(const char *name, const char *i2c_dev, uint8_t addr)
{
    struct driver_info *drv;
    struct nfc_args_i2c args;
    char driver[32];
    const char *instance = NULL;
    int r;

    if (!name || !i2c_dev)
        return NULL;

    /* default: name is instance, driver is "I2C" */
    strncpy(driver, "I2C", sizeof(driver) - 1);
    driver[sizeof(driver) - 1] = '\0';
    instance = name;

    r = split_driver_instance(name, driver, sizeof(driver), &instance);
    if (r < 0)
        return NULL;

    drv = find_driver(driver, NFC_DRV_I2C);
    if (!drv || !drv->factory)
        return NULL;

    args.instance = instance;
    args.dev_path = i2c_dev;
    args.addr = addr;
    return drv->factory(&args);
}

struct nfc_dev *nfc_alloc_spi(const char *name, const char *spi_dev, uint32_t cs_pin)
{
    struct driver_info *drv;
    struct nfc_args_spi args;
    char driver[32];
    const char *instance = NULL;
    int r;

    if (!name || !spi_dev)
        return NULL;

    strncpy(driver, "SPI", sizeof(driver) - 1);
    driver[sizeof(driver) - 1] = '\0';
    instance = name;

    r = split_driver_instance(name, driver, sizeof(driver), &instance);
    if (r < 0)
        return NULL;

    drv = find_driver(driver, NFC_DRV_SPI);
    if (!drv || !drv->factory)
        return NULL;

    args.instance = instance;
    args.dev_path = spi_dev;
    args.cs_pin = cs_pin;
    return drv->factory(&args);
}

struct nfc_dev *nfc_alloc_uart(const char *name, const char *uart_dev, uint32_t baud)
{
    struct driver_info *drv;
    struct nfc_args_uart args;
    char driver[32];
    const char *instance = NULL;
    int r;

    if (!name || !uart_dev)
        return NULL;

    strncpy(driver, "UART", sizeof(driver) - 1);
    driver[sizeof(driver) - 1] = '\0';
    instance = name;

    r = split_driver_instance(name, driver, sizeof(driver), &instance);
    if (r < 0)
        return NULL;

    drv = find_driver(driver, NFC_DRV_UART);
    if (!drv || !drv->factory)
        return NULL;

    args.instance = instance;
    args.dev_path = uart_dev;
    args.baud = baud;
    return drv->factory(&args);
}
