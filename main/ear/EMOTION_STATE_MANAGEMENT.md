# 情绪状态管理解决方案

## 问题分析

### 原有问题
1. **重复触发**：每次收到相同情绪时都会重新触发耳朵动作
2. **持续动作**：某些情绪（如"sad"、"sleepy"）设置为不自动停止，会持续动作
3. **缺乏状态管理**：没有跟踪当前情绪状态，无法避免重复触发
4. **不符合拟人特性**：真实的大象不会因为相同情绪持续摆动耳朵

### 具体场景
- 用户说"我很开心"，大象耳朵摆动
- 用户继续说"真的很开心"，大象耳朵再次摆动（不合理）
- 用户说"我很难过"，大象耳朵下垂
- 用户继续说"真的很难过"，大象耳朵保持下垂（合理）

## 解决方案

### 1. 情绪状态跟踪
在`Tc118sEarController`中添加状态跟踪变量：
```cpp
// 情绪状态跟踪
std::string current_emotion_;           // 当前情绪
uint64_t last_emotion_time_;           // 上次情绪触发时间
bool emotion_action_active_;           // 情绪动作是否正在进行
static const uint32_t EMOTION_COOLDOWN_MS = 3000; // 3秒冷却时间
```

### 2. 情绪触发条件检查
实现`ShouldTriggerEmotion()`方法：
```cpp
bool Tc118sEarController::ShouldTriggerEmotion(const char* emotion) {
    // 1. 检查冷却期：相同情绪3秒内不重复触发
    if (current_emotion_ == emotion && 
        (current_time - last_emotion_time_) < EMOTION_COOLDOWN_MS) {
        return false;
    }
    
    // 2. 检查动作状态：当前有动作进行时不触发新动作
    if (emotion_action_active_) {
        return false;
    }
    
    return true;
}
```

### 3. 情绪状态更新
实现`UpdateEmotionState()`方法：
```cpp
void Tc118sEarController::UpdateEmotionState(const char* emotion) {
    current_emotion_ = emotion;
    last_emotion_time_ = esp_timer_get_time() / 1000;
    emotion_action_active_ = true;
}
```

### 4. 场景完成时重置状态
在场景完成时重置情绪动作状态：
```cpp
if (!current_scenario_.loop_enabled || 
    current_loop_count_ >= current_scenario_.loop_count) {
    scenario_active_ = false;
    emotion_action_active_ = false; // 重置情绪动作状态
    StopBoth();
}
```

### 5. Application层情绪变化检测
在Application中添加情绪变化检测：
```cpp
// 检查情绪是否发生变化
static std::string last_emotion = "";
if (last_emotion != emotion_str) {
    // 情绪发生变化，触发耳朵动作
    ear_controller->TriggerByEmotion(emotion_str.c_str());
} else {
    // 情绪未变化，只更新显示，不触发耳朵动作
    display->SetEmotion(emotion_str.c_str());
}
```

## 实现效果

### 情绪触发逻辑
| 情况 | 行为 | 说明 |
|------|------|------|
| 新情绪 | 触发耳朵动作 | 情绪发生变化时正常触发 |
| 相同情绪（3秒内） | 跳过触发 | 避免重复动作 |
| 动作进行中 | 跳过触发 | 避免动作冲突 |
| 动作完成后 | 允许新触发 | 恢复正常触发 |

### 具体场景示例

#### 场景1：连续相同情绪
```
用户: "我很开心"
系统: 触发happy情绪 → 耳朵摆动2秒
用户: "真的很开心" (3秒内)
系统: 跳过触发 → 耳朵保持静止
用户: "超级开心" (3秒后)
系统: 触发happy情绪 → 耳朵再次摆动
```

#### 场景2：情绪变化
```
用户: "我很开心"
系统: 触发happy情绪 → 耳朵摆动
用户: "突然很难过"
系统: 触发sad情绪 → 耳朵下垂
用户: "现在又开心了"
系统: 触发happy情绪 → 耳朵摆动
```

#### 场景3：动作进行中
```
用户: "我很开心"
系统: 开始happy动作
用户: "真的很开心" (动作进行中)
系统: 跳过触发 → 等待当前动作完成
```

## 技术特性

### 1. 冷却机制
- **冷却时间**：3秒
- **作用**：避免相同情绪重复触发
- **可配置**：通过`EMOTION_COOLDOWN_MS`调整

### 2. 状态管理
- **情绪跟踪**：记录当前情绪和触发时间
- **动作状态**：跟踪动作是否正在进行
- **自动重置**：动作完成后自动重置状态

### 3. 分层处理
- **Application层**：检测情绪变化
- **Controller层**：管理触发条件
- **场景层**：执行具体动作

### 4. 日志记录
- **详细日志**：记录所有状态变化
- **调试信息**：便于问题排查
- **性能监控**：跟踪触发频率

## 配置参数

### 可调整参数
```cpp
static const uint32_t EMOTION_COOLDOWN_MS = 3000; // 冷却时间
```

### 情绪映射配置
```cpp
{"happy", {EAR_SCENARIO_GENTLE_HAPPY, 2000, true}},    // 2秒，自动停止
{"sad", {EAR_SCENARIO_SAD, 0, false}},                 // 持续，手动停止
{"sleepy", {EAR_SCENARIO_SLEEPY, 0, false}},           // 持续，手动停止
```

## 预期效果

1. **更自然的拟人表现**：避免重复动作，符合真实行为
2. **更好的用户体验**：动作有节制，不会过度频繁
3. **更稳定的系统**：避免动作冲突和资源浪费
4. **更智能的响应**：只在情绪真正变化时响应

## 测试建议

1. **连续相同情绪测试**：验证冷却机制
2. **快速情绪切换测试**：验证状态管理
3. **长时间运行测试**：验证稳定性
4. **边界条件测试**：验证异常处理
