# 拟人化耳朵控制系统使用指南

## 概述

本系统为astronaut-toys-esp32s3玩具设计了一个高度拟人化的耳朵控制系统，使用TC118S直流电机驱动芯片控制左右耳朵的摆动。系统不仅支持基础的向前、向后、停止动作，还提供了丰富的拟人化场景模式，让玩具的耳朵动作更加生动有趣。

## 硬件连接

### TC118S芯片连接
```
左耳朵电机：
- ESP32 GPIO15 -> TC118S INA (左耳)
- ESP32 GPIO16 -> TC118S INB (左耳)
- 电机电源 -> TC118S OUTA/OUTB (左耳)

右耳朵电机：
- ESP32 GPIO17 -> TC118S INA (右耳)
- ESP32 GPIO18 -> TC118S INB (右耳)
- 电机电源 -> TC118S OUTA/OUTB (右耳)
```

### TC118S控制逻辑表
| INA | INB | OUTA | OUTB | 方式 |
|-----|-----|------|------|------|
| L   | L   | Hi-Z | Hi-Z | 待命状态 |
| H   | L   | H    | L    | 前进 |
| L   | H   | L    | H    | 后退 |
| H   | H   | L    | L    | 刹车 |

## 功能特性

### 1. 基础控制功能
- **方向控制**：向前、向后、停止、刹车
- **速度控制**：慢速、正常、快速、极快
- **时间控制**：精确控制运动持续时间
- **独立控制**：左右耳朵可独立控制

### 2. 拟人化场景模式

#### 躲猫猫模式 (EAR_SCENARIO_PEEKABOO)
- **动作**：双耳长时间向前摆动，盖住眼睛
- **应用场景**：玩躲猫猫游戏时
- **持续时间**：可自定义（建议5-10秒）

#### 蚊虫叮咬模式 (EAR_SCENARIO_INSECT_BITE)
- **动作**：单边耳朵快速来回摆动
- **应用场景**：模拟耳朵被蚊虫叮咬后的反应
- **特点**：可选择左耳或右耳，快速摆动模拟抖落动作

#### 好奇模式 (EAR_SCENARIO_CURIOUS)
- **动作**：双耳交替摆动，模拟好奇的表情
- **应用场景**：听到声音或看到新事物时
- **特点**：左右耳朵交替运动，增加生动感

#### 困倦模式 (EAR_SCENARIO_SLEEPY)
- **动作**：耳朵缓慢下垂
- **应用场景**：玩具进入睡眠状态时
- **特点**：缓慢的向下运动，营造困倦感

#### 兴奋模式 (EAR_SCENARIO_EXCITED)
- **动作**：双耳快速交替摆动
- **应用场景**：玩具兴奋或高兴时
- **特点**：快速摆动，表现兴奋情绪

#### 伤心模式 (EAR_SCENARIO_SAD)
- **动作**：耳朵下垂
- **应用场景**：玩具伤心或沮丧时
- **特点**：缓慢下垂，表现低落情绪

#### 警觉模式 (EAR_SCENARIO_ALERT)
- **动作**：耳朵快速竖起
- **应用场景**：玩具警觉或听到声音时
- **特点**：快速向上运动，表现警觉状态

#### 玩耍模式 (EAR_SCENARIO_PLAYFUL)
- **动作**：不规则摆动，模拟玩耍时的随意动作
- **应用场景**：玩具玩耍时
- **特点**：不规则的摆动模式，增加趣味性

## API使用指南

### 初始化
```c
#include "ear_controller.h"

// 初始化耳朵控制器
esp_err_t ret = ear_controller_init();
if (ret != ESP_OK) {
    // 处理初始化错误
}
```

### 基础控制
```c
// 设置左耳向前摆动
ear_set_direction(true, EAR_FORWARD);

// 设置右耳向后摆动
ear_set_direction(false, EAR_BACKWARD);

// 设置左耳速度
ear_set_speed(true, EAR_SPEED_FAST);

// 停止左耳
ear_stop(true);

// 停止双耳
ear_stop_both();
```

### 定时控制
```c
// 左耳向前摆动3秒
ear_move_timed(true, EAR_FORWARD, EAR_SPEED_NORMAL, 3000);

// 双耳向后摆动2秒
ear_move_both_timed(EAR_BACKWARD, EAR_SPEED_FAST, 2000);
```

### 场景模式
```c
// 播放躲猫猫模式
ear_play_scenario(EAR_SCENARIO_PEEKABOO);

// 播放蚊虫叮咬模式（左耳）
ear_insect_bite_mode(true, 3000);

// 播放好奇模式
ear_curious_mode(5000);

// 播放兴奋模式
ear_excited_mode(4000);
```

### 自定义模式
```c
// 创建自定义摆动模式
ear_movement_step_t custom_steps[] = {
    {EAR_FORWARD, EAR_SPEED_SLOW, 1000, 500},      // 慢速向前1秒
    {EAR_BACKWARD, EAR_SPEED_FAST, 500, 200},      // 快速向后0.5秒
    {EAR_FORWARD, EAR_SPEED_VERY_FAST, 300, 100},  // 极快向前0.3秒
};

ear_scenario_config_t custom_config = {
    .scenario = EAR_SCENARIO_CUSTOM,
    .steps = custom_steps,
    .step_count = 3,
    .loop_enabled = true,
    .loop_count = 2
};

ear_set_custom_scenario(&custom_config);
ear_play_scenario(EAR_SCENARIO_CUSTOM);
```

## 集成到主程序

### 1. 在main函数中初始化
```c
void app_main(void) {
    // 其他初始化代码...
    
    // 初始化耳朵控制器
    ear_controller_init();
    
    // 启动演示任务
    ear_example_init();
}
```

### 2. 与触摸检测集成
```c
// 在触摸检测回调中
void touch_callback(touch_pad_t touch_num, uint16_t value) {
    switch (touch_num) {
        case TOUCH_CHANNEL_BELLY:
            // 触摸肚子，触发兴奋模式
            ear_excited_mode(3000);
            break;
        case TOUCH_CHANNEL_HEAD:
            // 触摸头部，触发好奇模式
            ear_curious_mode(2000);
            break;
        case TOUCH_CHANNEL_LEFT_EAR:
            // 触摸左耳，触发蚊虫叮咬模式
            ear_insect_bite_mode(true, 2000);
            break;
        case TOUCH_CHANNEL_RIGHT_EAR:
            // 触摸右耳，触发蚊虫叮咬模式
            ear_insect_bite_mode(false, 2000);
            break;
    }
}
```

### 3. 与情绪系统集成
```c
// 情绪状态管理
typedef enum {
    EMOTION_NEUTRAL = 0,
    EMOTION_HAPPY,
    EMOTION_SAD,
    EMOTION_EXCITED,
    EMOTION_SLEEPY,
    EMOTION_CURIOUS,
    EMOTION_ALERT
} emotion_state_t;

void update_emotion(emotion_state_t emotion) {
    switch (emotion) {
        case EMOTION_HAPPY:
            ear_playful_mode(3000);
            break;
        case EMOTION_SAD:
            ear_sad_mode();
            break;
        case EMOTION_EXCITED:
            ear_excited_mode(4000);
            break;
        case EMOTION_SLEEPY:
            ear_sleepy_mode();
            break;
        case EMOTION_CURIOUS:
            ear_curious_mode(3000);
            break;
        case EMOTION_ALERT:
            ear_alert_mode();
            break;
    }
}
```

## 调试和测试

### 1. 基础测试
```c
// 测试基础功能
void test_basic_functions(void) {
    // 测试左耳
    ear_move_timed(true, EAR_FORWARD, EAR_SPEED_NORMAL, 2000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    ear_move_timed(true, EAR_BACKWARD, EAR_SPEED_NORMAL, 2000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    
    // 测试右耳
    ear_move_timed(false, EAR_FORWARD, EAR_SPEED_NORMAL, 2000);
    vTaskDelay(pdMS_TO_TICKS(3000));
    ear_move_timed(false, EAR_BACKWARD, EAR_SPEED_NORMAL, 2000);
    vTaskDelay(pdMS_TO_TICKS(3000));
}
```

### 2. 场景测试
```c
// 测试所有场景模式
void test_all_scenarios(void) {
    ear_scenario_t scenarios[] = {
        EAR_SCENARIO_PEEKABOO,
        EAR_SCENARIO_INSECT_BITE,
        EAR_SCENARIO_CURIOUS,
        EAR_SCENARIO_SLEEPY,
        EAR_SCENARIO_EXCITED,
        EAR_SCENARIO_SAD,
        EAR_SCENARIO_ALERT,
        EAR_SCENARIO_PLAYFUL
    };
    
    for (int i = 0; i < sizeof(scenarios)/sizeof(scenarios[0]); i++) {
        ESP_LOGI(TAG, "Testing scenario: %d", scenarios[i]);
        ear_play_scenario(scenarios[i]);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
```

## 注意事项

1. **电源管理**：确保TC118S芯片有足够的电源供应
2. **电机保护**：避免长时间连续运行，防止电机过热
3. **GPIO配置**：确保GPIO引脚配置正确，避免冲突
4. **任务优先级**：合理设置FreeRTOS任务优先级
5. **内存管理**：注意动态内存分配，及时释放资源

## 扩展功能

### 1. 添加新的场景模式
```c
// 在ear_scenario_t枚举中添加新场景
typedef enum {
    // ... 现有场景 ...
    EAR_SCENARIO_NEW_MODE = 10
} ear_scenario_t;

// 在ear_play_scenario函数中添加处理逻辑
case EAR_SCENARIO_NEW_MODE:
    // 实现新的场景逻辑
    break;
```

### 2. 添加传感器集成
```c
// 集成声音传感器
void sound_detection_callback(void) {
    // 检测到声音，触发警觉模式
    ear_alert_mode();
}

// 集成光线传感器
void light_detection_callback(uint16_t light_level) {
    if (light_level < 100) {
        // 光线暗，触发困倦模式
        ear_sleepy_mode();
    }
}
```

### 3. 添加网络控制
```c
// 通过网络命令控制耳朵
void handle_ear_command(const char* command) {
    if (strcmp(command, "peekaboo") == 0) {
        ear_play_scenario(EAR_SCENARIO_PEEKABOO);
    } else if (strcmp(command, "excited") == 0) {
        ear_play_scenario(EAR_SCENARIO_EXCITED);
    }
    // ... 更多命令处理
}
```

## 与现有情绪系统的完美集成

### 自动情绪响应
本系统已经与astronaut-toys-esp32s3的现有情绪系统完美集成。当LLM返回情绪消息时，系统会自动：

1. **显示表情**：在屏幕上显示对应的emoji表情
2. **控制耳朵**：自动触发对应的耳朵动作

### 集成流程
```
LLM返回JSON消息 (type: "llm", emotion: "happy")
    ↓
Application::OnIncomingJson 处理
    ↓
display->SetEmotion("happy")  // 显示表情
ear_trigger_by_emotion("happy")  // 控制耳朵
    ↓
耳朵执行玩耍模式动作
```

### 默认情绪映射
系统为所有21种情绪都定义了对应的耳朵动作：

| 情绪 | 耳朵动作 | 持续时间 | 说明 |
|------|----------|----------|------|
| neutral | 停止 | 0ms | 正常状态 |
| happy | 玩耍模式 | 3000ms | 愉快摆动 |
| laughing | 兴奋模式 | 4000ms | 快速摆动 |
| funny | 玩耍模式 | 2500ms | 有趣摆动 |
| sad | 伤心模式 | 持续 | 耳朵下垂 |
| angry | 警觉模式 | 2000ms | 耳朵竖起 |
| crying | 伤心模式 | 持续 | 耳朵下垂 |
| loving | 好奇模式 | 2000ms | 交替摆动 |
| embarrassed | 伤心模式 | 1500ms | 短暂下垂 |
| surprised | 警觉模式 | 1000ms | 快速竖起 |
| shocked | 警觉模式 | 1500ms | 竖起保持 |
| thinking | 好奇模式 | 3000ms | 思考摆动 |
| winking | 玩耍模式 | 1500ms | 俏皮摆动 |
| cool | 警觉模式 | 1000ms | 酷炫竖起 |
| relaxed | 停止 | 0ms | 放松状态 |
| delicious | 兴奋模式 | 2000ms | 美味兴奋 |
| kissy | 好奇模式 | 1500ms | 亲昵摆动 |
| confident | 警觉模式 | 1000ms | 自信竖起 |
| sleepy | 困倦模式 | 持续 | 耳朵下垂 |
| silly | 玩耍模式 | 3000ms | 傻傻摆动 |
| confused | 好奇模式 | 2500ms | 困惑摆动 |

### 使用示例

#### 1. 自动集成（推荐）
系统已经自动集成，无需额外代码。当LLM返回情绪时，耳朵会自动响应：

```json
{
  "type": "llm",
  "emotion": "happy"
}
```

#### 2. 手动触发
```c
// 手动触发happy情绪的耳朵动作
ear_trigger_by_emotion("happy");

// 手动触发sad情绪的耳朵动作
ear_trigger_by_emotion("sad");
```

#### 3. 自定义映射
```c
// 自定义happy情绪映射为兴奋模式，持续5秒
ear_set_emotion_mapping("happy", EAR_SCENARIO_EXCITED, 5000);

// 自定义sad情绪映射为躲猫猫模式，持续3秒
ear_set_emotion_mapping("sad", EAR_SCENARIO_PEEKABOO, 3000);
```

#### 4. 情绪强度控制
```c
// 根据情绪强度调整动作
ear_trigger_by_emotion_with_intensity("happy", 0.8f);  // 高强度
ear_trigger_by_emotion_with_intensity("sad", 0.3f);    // 低强度
```

#### 5. 情绪转换
```c
// 从happy平滑转换到sad
ear_transition_emotion("happy", "sad", 2000);
```

### 测试集成系统
```c
// 在main函数中初始化测试
ear_emotion_test_init();
```

这个拟人化的耳朵控制系统与现有情绪系统的完美集成，为astronaut-toys-esp32s3玩具提供了前所未有的表情表达能力，让玩具更加生动有趣，大大增强了与用户的互动体验！
