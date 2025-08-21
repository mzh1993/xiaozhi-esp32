# 耳朵抖动问题解决方案

## 问题分析

### 抖动原因
1. **PWM实现过于复杂**：20ms周期的PWM导致频繁的GPIO切换
2. **延时过短**：动作时间和间隔太短，导致电机频繁启停
3. **随机性过度**：±50ms的随机延迟增加了不稳定性

### 日志分析
从日志可以看到频繁的GPIO状态切换：
```
I (59809) TC118S_EAR_CONTROLLER: SetGpioLevels: Right ear, direction=0
I (59809) TC118S_EAR_CONTROLLER: GPIO levels set: INA=0, INB=0 (STOP)
I (59829) TC118S_EAR_CONTROLLER: SetGpioLevels: Right ear, direction=2
I (59829) TC118S_EAR_CONTROLLER: GPIO levels set: INA=0, INB=1 (BACKWARD)
```

## 解决方案

### 1. 简化速度控制实现
**原实现**：复杂的PWM控制，频繁GPIO切换
```cpp
// 原PWM实现
uint32_t pwm_period = 20; // 20ms PWM周期
for (uint32_t i = 0; i < cycles; i++) {
    vTaskDelay(pdMS_TO_TICKS(on_time));
    SetDirection(left_ear, EAR_STOP);
    vTaskDelay(pdMS_TO_TICKS(off_time));
    SetDirection(left_ear, direction);
}
```

**新实现**：简化的时间控制，避免频繁切换
```cpp
// 简化的速度控制
switch (speed) {
    case EAR_SPEED_SLOW:
        actual_duration = duration_ms * 2;  // 慢速时延长运行时间
        break;
    case EAR_SPEED_NORMAL:
        actual_duration = duration_ms;      // 正常速度
        break;
    case EAR_SPEED_FAST:
        actual_duration = duration_ms * 3 / 4;  // 快速时缩短运行时间
        break;
    case EAR_SPEED_VERY_FAST:
        actual_duration = duration_ms / 2;  // 极快时大幅缩短运行时间
        break;
}
vTaskDelay(pdMS_TO_TICKS(actual_duration));
```

### 2. 增加动作时间和间隔
**优化前**：
```cpp
{EAR_BACKWARD, EAR_SPEED_VERY_FAST, 100, 50},   // 动作时间太短
{EAR_FORWARD, EAR_SPEED_NORMAL, 600, 300},      // 间隔太短
```

**优化后**：
```cpp
{EAR_BACKWARD, EAR_SPEED_VERY_FAST, 150, 100},   // 增加动作时间
{EAR_FORWARD, EAR_SPEED_NORMAL, 800, 400},       // 增加间隔时间
```

### 3. 减少随机性
**优化前**：
```cpp
static std::uniform_int_distribution<> delay_var(-50, 50); // ±50ms随机延迟
```

**优化后**：
```cpp
static std::uniform_int_distribution<> delay_var(-20, 20); // ±20ms随机延迟
```

### 4. 增加循环间隔
**优化前**：
```cpp
vTaskDelay(pdMS_TO_TICKS(200));  // 200ms循环间隔
next_delay = 100;                // 100ms默认间隔
```

**优化后**：
```cpp
vTaskDelay(pdMS_TO_TICKS(300));  // 300ms循环间隔
next_delay = 150;                // 150ms默认间隔
```

## 具体优化内容

### 场景步骤优化
| 场景类型 | 动作时间 | 间隔时间 | 改进说明 |
|---------|---------|---------|----------|
| 蚊虫叮咬 | 100ms → 150ms | 50ms → 100ms | 增加50%时间，减少抖动 |
| 好奇模式 | 600ms → 800ms | 300ms → 400ms | 增加33%时间，更稳定 |
| 兴奋模式 | 200ms → 300ms | 150ms → 200ms | 增加50%时间，减少抖动 |
| 玩耍模式 | 400ms → 600ms | 200ms → 300ms | 增加50%时间，更自然 |
| 温和开心 | 800ms → 1000ms | 400ms → 500ms | 增加25%时间，更温和 |
| 惊讶模式 | 300ms → 400ms | 0ms → 0ms | 增加33%时间，更明显 |
| 困倦模式 | 1500ms → 1800ms | 0ms → 0ms | 增加20%时间，更缓慢 |
| 伤心模式 | 1200ms → 1500ms | 0ms → 0ms | 增加25%时间，更缓慢 |

### 速度控制优化
| 速度等级 | 原PWM占空比 | 新时间比例 | 改进说明 |
|---------|------------|-----------|----------|
| 慢速 | 30% | 200% | 延长运行时间，更稳定 |
| 正常 | 60% | 100% | 保持原有时间 |
| 快速 | 80% | 75% | 缩短运行时间，减少抖动 |
| 极快 | 100% | 50% | 大幅缩短时间，减少抖动 |

## 预期效果

1. **消除抖动**：通过简化PWM实现，避免频繁GPIO切换
2. **动作更稳定**：增加动作时间和间隔，减少电机启停频率
3. **更自然**：减少随机性，增加循环间隔，动作更流畅
4. **保持表现力**：在稳定的基础上保持丰富的情绪表达

## 测试建议

1. **编译测试**：确保所有修改正确编译
2. **功能测试**：测试各种场景模式，观察是否还有抖动
3. **长时间测试**：运行较长时间，观察稳定性
4. **对比测试**：与优化前对比，验证改进效果

## 注意事项

- 优化后动作时间略有增加，但更稳定
- 保持了所有场景的完整功能
- 速度控制逻辑简化，但效果更可靠
- 随机性减少，但保持了自然的动作变化
