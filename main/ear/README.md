# 耳朵控制器 (Ear Controller)

基于Display架构设计的耳朵控制器驱动系统，为astronaut-toys-esp32s3玩具提供拟人化的耳朵动作控制。

## 架构设计

### 抽象化层次结构
```
EarController (抽象基类)
├── Tc118sEarController (TC118S芯片具体实现)
├── NoEarController (空实现，用于测试)
└── 其他EarController实现...
```

### 架构层次
```
┌─────────────────────────────────────┐
│           Application层              │
│  (业务逻辑，情绪处理，事件调度)        │
├─────────────────────────────────────┤
│           Board层                    │
│  (硬件抽象，设备初始化，配置管理)      │
├─────────────────────────────────────┤
│        EarController抽象层           │
│  (接口定义，通用功能，多态实现)        │
├─────────────────────────────────────┤
│      具体EarController实现层         │
│  (TC118S, 其他芯片等具体实现)         │
└─────────────────────────────────────┘
```

## 核心特性

### 1. 基础控制功能
- **方向控制**: 向前、向后、停止、刹车
- **速度控制**: 慢速、正常、快速、极快
- **定时控制**: 精确的时间控制
- **独立控制**: 左右耳朵独立控制

### 2. 场景模式
- **躲猫猫模式**: 双耳长时间向前运动盖住眼睛
- **蚊虫叮咬模式**: 单边耳朵快速来回摆动
- **好奇模式**: 双耳交替摆动
- **困倦模式**: 缓慢下垂
- **兴奋模式**: 快速摆动
- **伤心模式**: 耳朵下垂
- **警觉模式**: 耳朵竖起
- **玩耍模式**: 不规则摆动

### 3. 情绪集成
- **自动情绪响应**: 与现有情绪系统完美集成
- **情绪映射**: 21种情绪到耳朵动作的映射
- **情绪转换**: 平滑的情绪过渡
- **强度控制**: 根据情绪强度调整动作

### 4. 高级功能
- **自定义模式**: 支持自定义动作序列
- **异步执行**: 非阻塞的场景播放
- **状态管理**: 完整的耳朵状态跟踪
- **错误处理**: 完善的错误处理机制

## 硬件支持

### TC118S芯片
- **控制逻辑**: 基于TC118S数据手册的正确控制逻辑
- **GPIO配置**: 支持自定义GPIO引脚配置
- **电机驱动**: 支持有刷直流电机驱动

### 引脚配置
```cpp
// 在config.h中定义
#define LEFT_EAR_INA_GPIO   GPIO_NUM_15   // 左耳朵电机控制引脚A
#define LEFT_EAR_INB_GPIO   GPIO_NUM_16   // 左耳朵电机控制引脚B
#define RIGHT_EAR_INA_GPIO  GPIO_NUM_17   // 右耳朵电机控制引脚A
#define RIGHT_EAR_INB_GPIO  GPIO_NUM_18   // 右耳朵电机控制引脚B
```

## 使用方法

### 1. 基础使用
```cpp
// 获取耳朵控制器
auto ear_controller = Board::GetInstance().GetEarController();

// 基础控制
ear_controller->SetDirection(true, EAR_FORWARD);   // 左耳向前
ear_controller->SetDirection(false, EAR_BACKWARD); // 右耳向后
ear_controller->StopBoth();                        // 停止双耳

// 定时控制
ear_controller->MoveTimed(true, EAR_FORWARD, EAR_SPEED_NORMAL, 2000);
```

### 2. 场景模式
```cpp
// 播放预设场景
ear_controller->PeekabooMode(5000);      // 躲猫猫模式5秒
ear_controller->CuriousMode(3000);       // 好奇模式3秒
ear_controller->ExcitedMode(2000);       // 兴奋模式2秒
ear_controller->PlayfulMode(4000);       // 玩耍模式4秒
```

### 3. 情绪集成
```cpp
// 设置情绪映射
ear_controller->SetEmotionMapping("happy", EAR_SCENARIO_PLAYFUL, 3000);
ear_controller->SetEmotionMapping("sad", EAR_SCENARIO_SAD, 0);

// 触发情绪动作
ear_controller->TriggerByEmotion("happy");
ear_controller->TriggerByEmotion("sad");
```

### 4. 自定义模式
```cpp
// 定义自定义动作序列
ear_movement_step_t custom_steps[] = {
    {EAR_FORWARD, EAR_SPEED_NORMAL, 500, 200},
    {EAR_BACKWARD, EAR_SPEED_FAST, 300, 100},
    {EAR_FORWARD, EAR_SPEED_VERY_FAST, 200, 50}
};

// 播放自定义模式
ear_controller->PlayCustomPattern(custom_steps, 3, true);
```

## 集成到Board

### 1. 在Board中初始化
```cpp
void InitializeEarController() {
    // 创建TC118S耳朵控制器实例
    ear_controller_ = new Tc118sEarController(
        LEFT_EAR_INA_GPIO, LEFT_EAR_INB_GPIO,
        RIGHT_EAR_INA_GPIO, RIGHT_EAR_INB_GPIO
    );
    
    // 初始化耳朵控制器
    esp_err_t ret = ear_controller_->Initialize();
    if (ret != ESP_OK) {
        // 失败时使用空实现
        delete ear_controller_;
        ear_controller_ = new NoEarController();
        ear_controller_->Initialize();
    }
}

// 提供访问接口
virtual EarController* GetEarController() override {
    return ear_controller_;
}
```

### 2. 在Application中使用
```cpp
// 在Application::Start()中初始化情绪映射
auto ear_controller = Board::GetInstance().GetEarController();
if (ear_controller) {
    ear_controller->SetEmotionMapping("happy", EAR_SCENARIO_PLAYFUL, 3000);
    ear_controller->SetEmotionMapping("sad", EAR_SCENARIO_SAD, 0);
    // ... 其他情绪映射
}

// 在OnIncomingJson中触发情绪动作
} else if (strcmp(type->valuestring, "llm") == 0) {
    auto emotion = cJSON_GetObjectItem(root, "emotion");
    if (cJSON_IsString(emotion)) {
        Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
            display->SetEmotion(emotion_str.c_str());
            // 触发对应的耳朵动作
            auto ear_controller = Board::GetInstance().GetEarController();
            if (ear_controller) {
                ear_controller->TriggerByEmotion(emotion_str.c_str());
            }
        });
    }
}
```

## 情绪映射表

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
| winking | 玩耍模式 | 1500ms | 眨眼摆动 |
| cool | 警觉模式 | 1000ms | 酷炫竖起 |
| relaxed | 正常状态 | 0ms | 放松状态 |
| delicious | 兴奋模式 | 2000ms | 美味摆动 |
| kissy | 好奇模式 | 1500ms | 亲吻摆动 |
| confident | 警觉模式 | 1000ms | 自信竖起 |
| sleepy | 困倦模式 | 持续 | 耳朵下垂 |
| silly | 玩耍模式 | 3000ms | 傻傻摆动 |
| confused | 好奇模式 | 2500ms | 困惑摆动 |

## 测试和调试

### 1. 使用示例
```cpp
// 运行演示
ear_example_init();

// 或者单独测试
void test_ear_controller() {
    auto ear_controller = Board::GetInstance().GetEarController();
    
    // 基础测试
    ear_controller->SetDirection(true, EAR_FORWARD);
    vTaskDelay(pdMS_TO_TICKS(1000));
    ear_controller->StopBoth();
    
    // 场景测试
    ear_controller->PeekabooMode(3000);
    vTaskDelay(pdMS_TO_TICKS(4000));
    
    // 情绪测试
    ear_controller->TriggerByEmotion("happy");
    vTaskDelay(pdMS_TO_TICKS(3000));
}
```

### 2. 调试信息
- 所有操作都有详细的日志输出
- 支持状态查询和监控
- 错误处理和恢复机制

## 扩展开发

### 1. 添加新的EarController实现
```cpp
class NewEarController : public EarController {
public:
    // 实现抽象方法
    virtual esp_err_t Initialize() override;
    virtual esp_err_t Deinitialize() override;
    virtual void SetGpioLevels(bool left_ear, ear_direction_t direction) override;
    
    // 重写需要的方法
    virtual esp_err_t SetDirection(bool left_ear, ear_direction_t direction) override;
    // ... 其他方法
};
```

### 2. 添加新的场景模式
```cpp
// 在ear_scenario_t枚举中添加新场景
typedef enum {
    // ... 现有场景
    EAR_SCENARIO_NEW_MODE = 10,  // 新场景
} ear_scenario_t;

// 在具体实现中添加场景逻辑
esp_err_t NewEarController::PlayScenario(ear_scenario_t scenario) {
    switch (scenario) {
        // ... 现有场景
        case EAR_SCENARIO_NEW_MODE:
            // 实现新场景逻辑
            break;
    }
}
```

## 注意事项

1. **GPIO配置**: 确保GPIO引脚配置正确，避免冲突
2. **电源管理**: 电机驱动需要足够的电源供应
3. **时序控制**: 注意电机启动和停止的时序
4. **温度控制**: 长时间运行需要注意电机温度
5. **错误处理**: 实现完善的错误处理和恢复机制

## 许可证

本项目遵循与主项目相同的许可证。
