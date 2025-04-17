# ESP-SparkSpot

## 简介

ESP-SparkSpot 是一个基于 ESP32-S3 的智能语音交互设备，使用 ES8311 音频解码器实现双声道无MCLK音频处理功能。该项目去除了摄像头相关功能，专注于提供高质量的音频交互体验。

## 硬件特点

- 基于 ESP32-S3 主控芯片
- 使用 ES8311 音频编解码器，支持双声道音频输入/输出
- 无需MCLK连接，简化硬件设计
- 支持功放使能控制
- 音频电源管理功能，降低功耗

## 引脚配置

| 功能描述 | 引脚定义 |
|---------|---------|
| I2S BCLK | GPIO_NUM_16 |
| I2S WS   | GPIO_NUM_17 |
| I2S DIN  | GPIO_NUM_15 |
| I2S DOUT | GPIO_NUM_18 |
| I2C SDA  | GPIO_NUM_2  |
| I2C SCL  | GPIO_NUM_1  |
| 功放使能 | GPIO_NUM_40 |
| 音频电源 | GPIO_NUM_6  |
| BOOT按钮 | GPIO_NUM_0  |

## 配置、编译命令

**配置编译目标为 ESP32S3**

```bash
idf.py set-target esp32s3
```

**打开 menuconfig 并配置**

```bash
idf.py menuconfig
```

分别配置如下选项：

- `Xiaozhi Assistant` → `Board Type` → 选择 `ESP-SparkSpot`
- `Partition Table` → `Custom partition CSV file` → 删除原有内容，输入 `partitions_8M.csv`
- `Serial flasher config` → `Flash size` → 选择 `8 MB`

按 `S` 保存，按 `Q` 退出。

**编译**

```bash
idf.py build
```

**烧录**

将设备连接到电脑，然后执行：

```bash
idf.py flash
```

烧录完毕后，按一下 RESET 按钮重启设备。

## 双声道音频特性

ESP-SparkSpot 支持双声道音频处理，无需外部MCLK时钟源。音频配置如下：

- 采样率：16kHz
- 位宽：16位
- 通道数：双声道
- BCLK频率：16kHz × 16位 × 2通道 = 512kHz

## 电源管理

设备实现了智能电源管理功能：
- 在音频输入/输出启用时自动开启电源
- 在音频功能停用后延迟关闭电源，节省功耗
- 支持电源故障恢复机制 