## 项目简介
    本组件基于 I2C 的 SI512 NFC 读卡驱动与示例程序，提供初始化、轮询标签、读取/写入块数据等能力。
    解决了在 Linux 用户态快速接入 I2C NFC 模块、验证读卡链路与数据交互的问题。

## 功能特性
    支持：
    - I2C 方式接入 SI512 设备并轮询 Type A 标签。
    - 获取 UID 与 RSSI 等标签信息。
    - 读取/写入卡块数据接口（具体设备能力由硬件与驱动实现决定）。
    不支持：
    - SPI/UART 设备接入（本示例未覆盖）。
    - 高级防冲突与协议扩展的完整实现（仅示例路径）。

## 快速开始
    以 test_nfc_i2c.c 为例，完成最短路径运行。

### 环境准备
    - Linux 设备具备 I2C 控制器与 SI512 模块。
    - 可访问 I2C 设备节点（如 /dev/i2c-5），需要适当权限。
    - 具备 GCC 编译环境。

### 构建编译
以下为脱离 SDK 的独立构建方式：
```bash
mkdir -p build
cd build
cmake ..
#开启drv_i2c_SI512，需要打开编译配置
#cmake -DBUILD_TESTS=true -DSROBOTIS_PERIPHERALS_NFC_ENABLED_DRIVERS="drv_i2c_SI512" ../
make -j
```

### 运行示例
    - 默认参数：
      ./test_nfc_i2c
    - 指定设备、地址与块号：
      ./test_nfc_i2c /dev/i2c-5 0x28 4

### 关键代码示例
    初始化、调用接口与释放：

```c
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

struct nfc_tag_info info;
int ret = nfc_poll(dev, &info, 100);
if (ret == 0) {
    uint8_t buf[16] = {0};
    (void)nfc_read_block(dev, block, buf, sizeof(buf));

    uint8_t wbuf[16] = {0x11, 0x22, 0x33, 0x44};
    (void)nfc_write_block(dev, block, wbuf, sizeof(wbuf));
}

nfc_free(dev);
```

## 详细使用
    详细使用请参考后续官方文档（待补充）。

## 常见问题
    - 无法打开 /dev/i2c-*：检查权限或加入 i2c 用户组。
    - 一直提示 no tag：确认天线与供电、卡片类型、距离与方向。
    - 设备地址不匹配：确认硬件地址配置与参数一致。
    - 无法创建 /var/lock 锁文件：需要写权限或调整锁目录宏。

## 版本与发布

| 版本   | 日期       | 说明 |
| ------ | ---------- | ---- |
| 0.1.0  | 2026-02-28 | 初始版本，支持 I2C + SI512 设备。 |

## 贡献方式

欢迎参与贡献：提交 Issue 反馈问题，或通过 Pull Request 提交代码。

- **编码规范**：本组件 C 代码遵循 [Google C++ 风格指南](https://google.github.io/styleguide/cppguide.html)（C 相关部分），请按该规范编写与修改代码，并尽量补充测试与说明。
- **提交前检查**：请在提交前运行本仓库的 lint 脚本，确保通过风格检查：
  ```bash
  # 在仓库根目录执行（检查全仓库）
  bash scripts/lint/lint_cpp.sh

  # 仅检查本组件
  bash scripts/lint/lint_cpp.sh components/peripherals/nfc
  ```
  脚本路径：`scripts/lint/lint_cpp.sh`。若未安装 `cpplint`，可先执行：`pip install cpplint` 或 `pipx install cpplint`。
- **提交说明**：提交前请描述设备型号、I2C 连接与复现步骤。

## License

本组件源码文件头声明为 Apache-2.0，最终以本目录 `LICENSE` 文件为准。
