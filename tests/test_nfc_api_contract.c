/*
 * Copyright (C) 2026 SpacemiT (Hangzhou) Technology Co. Ltd.
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "nfc_core.h"

#define NFC_BLOCK_SIZE 16
#define NFC_TEST_BLOCKS 8

struct mock_nfc_priv {
    int initialized;
    uint8_t memory[NFC_TEST_BLOCKS][NFC_BLOCK_SIZE];
};

static int g_failures;
static int g_callback_count;
static int g_free_count;
static struct nfc_tag_info g_last_callback_info;

#define CHECK_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL:%s:%d: expected true: %s\n", __FILE__, __LINE__, #expr); \
        g_failures++; \
    } \
} while (0)

#define CHECK_INT_EQ(actual, expected) do { \
    int _actual = (int)(actual); \
    int _expected = (int)(expected); \
    if (_actual != _expected) { \
        printf("FAIL:%s:%d: expected %s == %d, got %d\n", \
            __FILE__, __LINE__, #actual, _expected, _actual); \
        g_failures++; \
    } \
} while (0)

#define CHECK_MEM_EQ(actual, expected, len) do { \
    if (memcmp((actual), (expected), (len)) != 0) { \
        printf("FAIL:%s:%d: memory differs: %s\n", __FILE__, __LINE__, #actual); \
        g_failures++; \
    } \
} while (0)

static void record_callback(struct nfc_dev *dev,
    const struct nfc_tag_info *info, void *ctx)
{
    int *seen = (int *)ctx;

    CHECK_TRUE(dev != NULL);
    CHECK_TRUE(info != NULL);
    CHECK_TRUE(seen != NULL);

    if (seen)
        (*seen)++;
    if (info) {
        g_last_callback_info = *info;
        g_callback_count++;
    }
}

static int mock_init(struct nfc_dev *dev)
{
    struct mock_nfc_priv *priv;

    if (!dev || !dev->priv_data)
        return -EINVAL;

    priv = dev->priv_data;
    priv->initialized = 1;
    return 0;
}

static int mock_poll(struct nfc_dev *dev,
    struct nfc_tag_info *info, uint32_t timeout_ms)
{
    struct mock_nfc_priv *priv;
    const uint8_t uid[] = {0xDE, 0xAD, 0xBE, 0xEF};

    (void)timeout_ms;

    if (!dev || !dev->priv_data || !info)
        return -EINVAL;

    priv = dev->priv_data;
    if (!priv->initialized)
        return -EAGAIN;

    memset(info, 0, sizeof(*info));
    memcpy(info->uid, uid, sizeof(uid));
    info->uid_len = sizeof(uid);
    info->type = NFC_TAG_MIFARE_CLASSIC;
    info->rssi_dbm = -42;
    info->ats[0] = 0x75;
    info->ats_len = 1;

    if (dev->cb)
        dev->cb(dev, info, dev->cb_ctx);

    return 0;
}

static int mock_read_block(struct nfc_dev *dev,
    uint8_t block, uint8_t *buf, size_t len)
{
    struct mock_nfc_priv *priv;

    if (!dev || !dev->priv_data || !buf)
        return -EINVAL;

    priv = dev->priv_data;
    if (!priv->initialized)
        return -EAGAIN;
    if (len != NFC_BLOCK_SIZE || block >= NFC_TEST_BLOCKS)
        return -EINVAL;

    memcpy(buf, priv->memory[block], NFC_BLOCK_SIZE);
    return NFC_BLOCK_SIZE;
}

static int mock_write_block(struct nfc_dev *dev,
    uint8_t block, const uint8_t *buf, size_t len)
{
    struct mock_nfc_priv *priv;

    if (!dev || !dev->priv_data || !buf)
        return -EINVAL;

    priv = dev->priv_data;
    if (!priv->initialized)
        return -EAGAIN;
    if (len != NFC_BLOCK_SIZE || block >= NFC_TEST_BLOCKS)
        return -EINVAL;

    memcpy(priv->memory[block], buf, NFC_BLOCK_SIZE);
    return NFC_BLOCK_SIZE;
}

static void mock_free(struct nfc_dev *dev)
{
    if (!dev)
        return;

    g_free_count++;
    free(dev->priv_data);
    free((void *)dev->name);
    free(dev);
}

static const struct nfc_ops mock_ops = {
    .init = mock_init,
    .poll = mock_poll,
    .read_block = mock_read_block,
    .write_block = mock_write_block,
    .free = mock_free,
};

static struct nfc_dev *mock_i2c_factory(void *args)
{
    struct nfc_args_i2c *i2c_args = args;
    struct nfc_dev *dev;
    struct mock_nfc_priv *priv;

    if (!i2c_args || !i2c_args->instance || !i2c_args->dev_path)
        return NULL;

    dev = nfc_dev_alloc(i2c_args->instance, sizeof(*priv));
    if (!dev)
        return NULL;

    dev->ops = &mock_ops;
    priv = dev->priv_data;
    for (uint8_t block = 0; block < NFC_TEST_BLOCKS; block++) {
        for (uint8_t index = 0; index < NFC_BLOCK_SIZE; index++)
            priv->memory[block][index] = (uint8_t)(block * NFC_BLOCK_SIZE + index);
    }

    return dev;
}

static struct nfc_dev *mock_spi_factory(void *args)
{
    (void)args;
    return NULL;
}

REGISTER_NFC_DRIVER("MOCK", NFC_DRV_I2C, mock_i2c_factory);
REGISTER_NFC_DRIVER("MOCKSPI", NFC_DRV_SPI, mock_spi_factory);

static void reset_test_state(void)
{
    g_failures = 0;
    g_callback_count = 0;
    g_free_count = 0;
    memset(&g_last_callback_info, 0, sizeof(g_last_callback_info));
}

static void test_public_api_rejects_invalid_inputs(void)
{
    struct nfc_dev *dev;
    struct nfc_tag_info info;
    uint8_t buf[NFC_BLOCK_SIZE];

    CHECK_INT_EQ(nfc_init(NULL), -1);
    CHECK_INT_EQ(nfc_poll(NULL, &info, 0), -1);
    CHECK_INT_EQ(nfc_read_block(NULL, 0, buf, sizeof(buf)), -1);
    CHECK_INT_EQ(nfc_write_block(NULL, 0, buf, sizeof(buf)), -1);
    nfc_set_callback(NULL, record_callback, NULL);
    nfc_free(NULL);

    CHECK_TRUE(nfc_alloc_i2c(NULL, "mock://nfc", 0x28) == NULL);
    CHECK_TRUE(nfc_alloc_i2c("MOCK:nfc0", NULL, 0x28) == NULL);
    CHECK_TRUE(nfc_alloc_i2c("MOCK:", "mock://nfc", 0x28) == NULL);
    CHECK_TRUE(nfc_alloc_i2c(":nfc0", "mock://nfc", 0x28) == NULL);
    CHECK_TRUE(nfc_alloc_i2c("MISSING:nfc0", "mock://nfc", 0x28) == NULL);
    CHECK_TRUE(nfc_alloc_i2c("MOCKSPI:nfc0", "mock://nfc", 0x28) == NULL);

    dev = nfc_alloc_i2c("MOCK:not-ready", "mock://nfc", 0x28);
    CHECK_TRUE(dev != NULL);
    if (!dev)
        return;

    CHECK_INT_EQ(nfc_poll(dev, &info, 0), -EAGAIN);
    CHECK_INT_EQ(nfc_poll(dev, NULL, 0), -1);
    CHECK_INT_EQ(nfc_read_block(dev, 0, NULL, sizeof(buf)), -1);
    CHECK_INT_EQ(nfc_write_block(dev, 0, NULL, sizeof(buf)), -1);
    CHECK_INT_EQ(nfc_read_block(dev, 0, buf, sizeof(buf)), -EAGAIN);
    CHECK_INT_EQ(nfc_write_block(dev, 0, buf, sizeof(buf)), -EAGAIN);
    nfc_free(dev);

    CHECK_INT_EQ(g_free_count, 1);
}

static void test_mock_driver_functional_flow(void)
{
    struct nfc_dev *dev;
    struct nfc_tag_info info;
    uint8_t expected_uid[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t read_buf[NFC_BLOCK_SIZE];
    uint8_t expected_block[NFC_BLOCK_SIZE];
    uint8_t write_buf[NFC_BLOCK_SIZE];
    int local_callback_count = 0;

    dev = nfc_alloc_i2c("MOCK:nfc0", "mock://nfc", 0x28);
    CHECK_TRUE(dev != NULL);
    if (!dev)
        return;

    nfc_set_callback(dev, record_callback, &local_callback_count);
    CHECK_INT_EQ(nfc_init(dev), 0);

    memset(&info, 0, sizeof(info));
    CHECK_INT_EQ(nfc_poll(dev, &info, 25), 0);
    CHECK_INT_EQ(info.uid_len, 4);
    CHECK_MEM_EQ(info.uid, expected_uid, sizeof(expected_uid));
    CHECK_INT_EQ(info.type, NFC_TAG_MIFARE_CLASSIC);
    CHECK_INT_EQ(info.rssi_dbm, -42);
    CHECK_INT_EQ(info.ats_len, 1);
    CHECK_INT_EQ(info.ats[0], 0x75);
    CHECK_INT_EQ(local_callback_count, 1);
    CHECK_INT_EQ(g_callback_count, 1);
    CHECK_MEM_EQ(g_last_callback_info.uid, expected_uid, sizeof(expected_uid));

    for (size_t index = 0; index < NFC_BLOCK_SIZE; index++)
        expected_block[index] = (uint8_t)(3 * NFC_BLOCK_SIZE + index);

    memset(read_buf, 0, sizeof(read_buf));
    CHECK_INT_EQ(nfc_read_block(dev, 3, read_buf, sizeof(read_buf)), NFC_BLOCK_SIZE);
    CHECK_MEM_EQ(read_buf, expected_block, sizeof(expected_block));

    for (size_t index = 0; index < NFC_BLOCK_SIZE; index++)
        write_buf[index] = (uint8_t)(0xA0 + index);

    CHECK_INT_EQ(nfc_write_block(dev, 3, write_buf, sizeof(write_buf)), NFC_BLOCK_SIZE);
    memset(read_buf, 0, sizeof(read_buf));
    CHECK_INT_EQ(nfc_read_block(dev, 3, read_buf, sizeof(read_buf)), NFC_BLOCK_SIZE);
    CHECK_MEM_EQ(read_buf, write_buf, sizeof(write_buf));

    CHECK_INT_EQ(nfc_read_block(dev, NFC_TEST_BLOCKS, read_buf, sizeof(read_buf)), -EINVAL);
    CHECK_INT_EQ(nfc_write_block(dev, NFC_TEST_BLOCKS, write_buf, sizeof(write_buf)), -EINVAL);
    CHECK_INT_EQ(nfc_read_block(dev, 3, read_buf, sizeof(read_buf) - 1), -EINVAL);
    CHECK_INT_EQ(nfc_write_block(dev, 3, write_buf, sizeof(write_buf) - 1), -EINVAL);

    nfc_free(dev);

    CHECK_INT_EQ(g_free_count, 1);
}

static int finish_test(const char *name)
{
    if (g_failures != 0) {
        printf("%s FAILED: %d failure(s)\n", name, g_failures);
        return 1;
    }

    printf("%s PASSED\n", name);
    return 0;
}

static int run_error_paths(void)
{
    reset_test_state();
    test_public_api_rejects_invalid_inputs();
    return finish_test("nfc api error paths test");
}

static int run_functional(void)
{
    reset_test_state();
    test_mock_driver_functional_flow();
    return finish_test("nfc api functional test");
}

int main(int argc, char **argv)
{
    if (argc <= 1 || strcmp(argv[1], "all") == 0) {
        if (run_error_paths() != 0)
            return 1;
        if (run_functional() != 0)
            return 1;
        printf("nfc api contract test PASSED\n");
        return 0;
    }

    if (strcmp(argv[1], "error-paths") == 0)
        return run_error_paths();

    if (strcmp(argv[1], "functional") == 0)
        return run_functional();

    fprintf(stderr, "usage: %s [all|functional|error-paths]\n", argv[0]);
    return 2;
}

