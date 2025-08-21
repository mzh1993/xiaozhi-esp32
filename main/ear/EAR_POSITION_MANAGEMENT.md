# 耳朵位置状态管理解决方案

## 问题分析

### 原有问题
1. **缺少位置状态跟踪**：没有监控耳朵的最终位置状态
2. **关机状态不确定**：关机时耳朵可能停留在竖立状态
3. **表情切换混乱**：不同表情之间的耳朵位置不协调
4. **不符合物理特性**：毛绒玩具的耳朵在静止时应该是下垂的

### 具体场景
- 设备开机时：耳朵应该处于下垂状态
- 设备关机时：耳朵必须回到下垂状态
- 表情切换时：耳朵位置应该与表情匹配
- 空闲状态时：耳朵应该回到下垂状态

## 解决方案

### 1. 耳朵位置状态定义
```cpp
typedef enum {
    EAR_POSITION_UNKNOWN = 0,   // 未知位置
    EAR_POSITION_DOWN = 1,      // 耳朵下垂（默认状态）
    EAR_POSITION_UP = 2,        // 耳朵竖起
    EAR_POSITION_MIDDLE = 3     // 耳朵中间位置
} ear_position_t;
```

### 2. 位置状态跟踪
```cpp
// 耳朵位置状态跟踪
ear_position_t left_ear_position_;
ear_position_t right_ear_position_;
ear_position_t target_left_ear_position_;
ear_position_t target_right_ear_position_;
```

### 3. 位置管理接口
```cpp
// 获取耳朵位置
virtual ear_position_t GetEarPosition(bool left_ear) = 0;

// 设置耳朵位置
virtual esp_err_t SetEarPosition(bool left_ear, ear_position_t position) = 0;

// 重置到默认位置
virtual esp_err_t ResetEarsToDefaultPosition() = 0;

// 确保耳朵下垂
virtual esp_err_t EnsureEarsDown() = 0;
```

### 4. 场景完成时的位置设置
```cpp
void SetEarFinalPosition() {
    switch (current_scenario_.scenario) {
        case EAR_SCENARIO_SAD:
        case EAR_SCENARIO_SLEEPY:
            // 伤心和困倦时耳朵下垂
            SetEarPosition(true, EAR_POSITION_DOWN);
            SetEarPosition(false, EAR_POSITION_DOWN);
            break;
            
        case EAR_SCENARIO_ALERT:
        case EAR_SCENARIO_SURPRISED:
            // 警觉和惊讶时耳朵竖起
            SetEarPosition(true, EAR_POSITION_UP);
            SetEarPosition(false, EAR_POSITION_UP);
            break;
            
        default:
            // 其他场景默认回到下垂状态
            SetEarPosition(true, EAR_POSITION_DOWN);
            SetEarPosition(false, EAR_POSITION_DOWN);
            break;
    }
}
```

### 5. 设备状态管理
```cpp
// 空闲状态时确保耳朵下垂
case kDeviceStateIdle:
    if (ear_controller) {
        ear_controller->EnsureEarsDown();
    }
    break;
```

### 6. 关机时的位置管理
```cpp
esp_err_t Deinitialize() {
    // 停止所有场景
    if (scenario_active_) {
        StopScenario();
    }
    
    // 确保耳朵回到下垂状态
    EnsureEarsDown();
    
    // 等待耳朵动作完成
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 停止所有耳朵
    StopBoth();
    
    return ESP_OK;
}
```

## 实现效果

### 位置状态映射
| 场景类型 | 最终位置 | 说明 |
|----------|----------|------|
| EAR_SCENARIO_SAD | DOWN | 伤心时耳朵下垂 |
| EAR_SCENARIO_SLEEPY | DOWN | 困倦时耳朵下垂 |
| EAR_SCENARIO_ALERT | UP | 警觉时耳朵竖起 |
| EAR_SCENARIO_SURPRISED | UP | 惊讶时耳朵竖起 |
| EAR_SCENARIO_PEEKABOO | UP | 躲猫猫时耳朵竖起 |
| 其他场景 | DOWN | 默认回到下垂状态 |

### 设备状态管理
| 设备状态 | 耳朵位置 | 说明 |
|----------|----------|------|
| 开机初始化 | DOWN | 确保初始状态正确 |
| 空闲状态 | DOWN | 符合毛绒玩具特性 |
| 关机终止 | DOWN | 确保最终状态正确 |
| 表情切换 | 根据表情 | 动态调整位置 |

### 位置切换逻辑
```cpp
switch (position) {
    case EAR_POSITION_DOWN:
        // 耳朵下垂 - 向后摆动
        MoveTimed(left_ear, EAR_BACKWARD, EAR_SPEED_SLOW, 800);
        break;
    case EAR_POSITION_UP:
        // 耳朵竖起 - 向前摆动
        MoveTimed(left_ear, EAR_FORWARD, EAR_SPEED_SLOW, 800);
        break;
    case EAR_POSITION_MIDDLE:
        // 根据当前位置调整到中间
        if (current_pos == EAR_POSITION_UP) {
            MoveTimed(left_ear, EAR_BACKWARD, EAR_SPEED_SLOW, 400);
        } else if (current_pos == EAR_POSITION_DOWN) {
            MoveTimed(left_ear, EAR_FORWARD, EAR_SPEED_SLOW, 400);
        }
        break;
}
```

## 技术特性

### 1. 状态一致性
- 实时跟踪耳朵位置状态
- 确保位置状态与实际动作一致
- 避免状态混乱和位置错误

### 2. 自动管理
- 场景完成时自动设置正确位置
- 设备状态变化时自动调整位置
- 关机时自动回到默认位置

### 3. 物理特性匹配
- 符合毛绒玩具的物理特性
- 耳朵在静止时自然下垂
- 动作完成后回到合理位置

### 4. 可配置性
- 不同场景可以设置不同的最终位置
- 位置切换时间和速度可调整
- 支持自定义位置映射

## 预期效果

1. **开机状态正确**：设备开机时耳朵处于下垂状态
2. **关机状态正确**：设备关机时耳朵回到下垂状态
3. **表情协调**：耳朵位置与表情状态匹配
4. **状态一致**：耳朵位置状态始终准确
5. **物理合理**：符合毛绒玩具的自然特性

## 测试建议

1. **开机测试**：验证开机时耳朵位置
2. **关机测试**：验证关机时耳朵位置
3. **表情切换测试**：验证不同表情的耳朵位置
4. **状态一致性测试**：验证位置状态与实际动作一致
5. **长时间运行测试**：验证状态管理的稳定性
