# Bread Compact WiFi 触摸按钮功能说明

## 概述

本项目已经集成了ESP32的触摸按钮功能，使用`components/touch_button`库来实现触摸按键检测。

## 硬件配置

### 实体按键（保留原有功能）
- `BOOT_BUTTON_GPIO` (GPIO 0): 启动按钮
- `TOUCH_BUTTON_GPIO` (GPIO 47): 主触摸按钮，用于语音交互
- `VOLUME_UP_BUTTON_GPIO` (GPIO 40): 音量增加按钮
- `VOLUME_DOWN_BUTTON_GPIO` (GPIO 39): 音量减少按钮

### 新增玩具触摸按键
- `TOUCH_CHANNEL_HEAD` (GPIO 3): 玩具头部触摸
- `TOUCH_CHANNEL_HAND` (GPIO 9): 玩具手部触摸
- `TOUCH_CHANNEL_BELLY` (GPIO 13): 玩具肚子触摸

### 触摸阈值设置
- 默认触摸阈值: 0.15 (15%)
- 可根据实际硬件调整阈值以获得最佳触摸响应

## 功能特性

### 实体按键功能（保留原有功能）
1. **启动按钮 (BOOT_BUTTON_GPIO)**
   - **单击**: 切换聊天状态
   - 在启动时未连接WiFi时重置WiFi配置

2. **主触摸按钮 (TOUCH_BUTTON_GPIO)**
   - **按下**: 开始语音监听
   - **释放**: 停止语音监听

3. **音量增加按钮 (VOLUME_UP_BUTTON_GPIO)**
   - **单击**: 音量增加10%
   - **长按**: 音量设置为100%

4. **音量减少按钮 (VOLUME_DOWN_BUTTON_GPIO)**
   - **单击**: 音量减少10%
   - **长按**: 音量设置为0%（静音）

### 新增玩具触摸按键功能
1. **头部触摸按钮 (TOUCH_CHANNEL_HEAD)**
   - **单击**: 显示"摸摸头~"提示
   - **长按**: 显示"长时间摸头~"提示

2. **手部触摸按钮 (TOUCH_CHANNEL_HAND)**
   - **单击**: 显示"握手手~"提示
   - **长按**: 显示"长时间握手~"提示

3. **肚子触摸按钮 (TOUCH_CHANNEL_BELLY)**
   - **单击**: 显示"摸摸肚子~"提示
   - **长按**: 显示"长时间摸肚子~"提示

## 代码实现

### 触摸按钮类 (TouchButtonWrapper)
位置: `main/boards/common/touch_button_wrapper.h` 和 `touch_button_wrapper.cc`

主要功能:
- 封装了ESP32触摸按钮的初始化和管理
- 提供事件回调接口（按下、释放、长按、单击等）
- 支持多个触摸通道的并发使用

### 使用示例

```cpp
// 创建玩具触摸按钮实例
TouchButtonWrapper head_touch_button(TOUCH_CHANNEL_HEAD, 0.15f);
TouchButtonWrapper hand_touch_button(TOUCH_CHANNEL_HAND, 0.15f);
TouchButtonWrapper belly_touch_button(TOUCH_CHANNEL_BELLY, 0.15f);

// 注册头部触摸事件回调
head_touch_button.OnClick([]() {
    ESP_LOGI(TAG, "Head touch button clicked");
    GetDisplay()->ShowNotification("摸摸头~");
});

head_touch_button.OnLongPress([]() {
    ESP_LOGI(TAG, "Head touch button long pressed");
    GetDisplay()->ShowNotification("长时间摸头~");
});

// 注册手部触摸事件回调
hand_touch_button.OnClick([]() {
    ESP_LOGI(TAG, "Hand touch button clicked");
    GetDisplay()->ShowNotification("握手手~");
});

// 注册肚子触摸事件回调
belly_touch_button.OnClick([]() {
    ESP_LOGI(TAG, "Belly touch button clicked");
    GetDisplay()->ShowNotification("摸摸肚子~");
});
```

## 编译和烧录

### 1. 依赖配置
项目已经配置了touch_button组件的依赖：
- 在`main/idf_component.yml`中添加了`espressif/touch_button: ^0.1.1`
- touch_button组件会自动管理其依赖的`espressif/button`和`espressif/touch_button_sensor`

### 2. 编译步骤
1. 确保项目包含touch_button组件
2. 配置正确的板型为`bread-compact-wifi`
3. 编译项目: `idf.py build`
4. 烧录到设备: `idf.py flash monitor`

### 3. 编译配置检查
运行以下命令检查编译配置：
```bash
python scripts/check_touch_button_build.py
```

## 调试

### 日志输出
触摸按钮事件会通过ESP_LOG输出调试信息:
```
I (1234) TouchButton: Touch button created for channel 47 with threshold 0.15
I (1235) TouchButton: Touch sensor initialized with 3 channels
I (1236) TouchButton: Touch sensor started successfully
I (1237) CompactWifiBoard: Touch button pressed down
I (1238) CompactWifiBoard: Touch button pressed up
```

### 常见问题

1. **触摸无响应**
   - 检查GPIO引脚配置是否正确
   - 调整触摸阈值（可能需要降低阈值）
   - 确认硬件连接正常

2. **误触发**
   - 增加触摸阈值
   - 检查电源稳定性
   - 确认没有电磁干扰

3. **编译错误**
   - 确保touch_button组件在components目录中
   - 检查依赖库是否正确安装

## 技术细节

### 触摸传感器初始化
```cpp
// 定义玩具触摸通道
uint32_t touch_channels[] = {TOUCH_CHANNEL_HEAD, TOUCH_CHANNEL_HAND, TOUCH_CHANNEL_BELLY};

// 初始化触摸传感器
TouchButton::InitializeTouchSensor(touch_channels, 3);
TouchButton::StartTouchSensor();
```

### 触摸按钮配置
```cpp
button_touch_config_t touch_config = {
    .touch_channel = TOUCH_CHANNEL_HEAD,
    .channel_threshold = 0.15f,
    .skip_lowlevel_init = true,
};
```

## 扩展功能

可以根据需要添加更多触摸按钮或修改现有功能:

1. 添加新的触摸通道
2. 实现双击检测
3. 添加手势识别
4. 实现触摸反馈（如LED指示）

## 参考资料

- [ESP32 Touch Sensor API](https://docs.espressif.com/projects/esp-idf/en/latest/api-reference/peripherals/touch_pad.html)
- [ESP-IDF Button Component](https://github.com/espressif/esp-bsp/tree/master/components/button)
- [Touch Button Component](https://github.com/espressif/esp-bsp/tree/master/components/touch_button)
