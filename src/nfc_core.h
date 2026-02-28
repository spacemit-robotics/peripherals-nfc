/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef NFC_CORE_H
#define NFC_CORE_H

/*
 * Private header for NFC component (motor-like minimal style).
 */

#include <stddef.h>

#include "../include/nfc.h"

/* 1. 参数适配包：用于将 alloc 参数打包成 void*（同时携带 instance 名称） */
struct nfc_args_i2c {
    const char *instance;
    const char *dev_path;
    uint8_t addr;
};

struct nfc_args_spi {
    const char *instance;
    const char *dev_path;
    uint32_t cs_pin;
};

struct nfc_args_uart {
    const char *instance;
    const char *dev_path;
    uint32_t baud;
};

/* 2. 驱动类型枚举 */
enum nfc_driver_type {
    NFC_DRV_I2C = 0,
    NFC_DRV_SPI,
    NFC_DRV_UART,
};

/* 3. 虚函数表（驱动实现） */
struct nfc_ops {
    int (*init)(struct nfc_dev *dev);
    int (*poll)(struct nfc_dev *dev, struct nfc_tag_info *info, uint32_t timeout_ms);
    int (*read_block)(struct nfc_dev *dev, uint8_t block, uint8_t *buf, size_t len);
    int (*write_block)(struct nfc_dev *dev, uint8_t block, const uint8_t *buf, size_t len);
    void (*free)(struct nfc_dev *dev);
};

/* 4. 设备对象（私有实现） */
struct nfc_dev {
    const char *name; /* instance name */
    const struct nfc_ops *ops;
    void *priv_data;
    nfc_event_cb_t cb;
    void *cb_ctx;
    struct nfc_tag_info *last_tag;
};

/* 5. 通用工厂函数类型 */
typedef struct nfc_dev *(*nfc_factory_t)(void *args);

/* 6. 注册节点结构 */
struct driver_info {
    const char *name;              /* driver name */
    enum nfc_driver_type type;     /* bus type */
    nfc_factory_t factory;
    struct driver_info *next;
};

void nfc_driver_register(struct driver_info *info);

#define REGISTER_NFC_DRIVER(_name, _type, _factory) \
    static struct driver_info __drv_info_##_factory = { \
        .name = _name, \
        .type = _type, \
        .factory = _factory, \
        .next = 0 \
    }; \
    __attribute__((constructor)) \
    static void __auto_reg_##_factory(void) { \
        nfc_driver_register(&__drv_info_##_factory); \
    }

struct nfc_dev *nfc_dev_alloc(const char *instance, size_t priv_size);

#endif /* NFC_CORE_H */
