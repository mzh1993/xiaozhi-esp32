# 耳朵控制系统使用示例

## 概述
经过重新设计，耳朵控制系统的接口更加简洁明了，使用起来更加直观。本文档展示了如何使用新的接口。

## 🎯 **接口设计原则**

### 1. **分层清晰**
- **基础层**：单耳控制，最底层的物理控制
- **组合层**：双耳协调，常用的组合动作
- **位置层**：位置控制，基于时间的位置定位
- **序列层**：动作序列，复杂的动作组合
- **应用层**：情绪触发，最高层的应用接口

### 2. **命名直观**
- `MoveEar()` - 移动单耳
- `MoveBoth()` - 移动双耳
- `PlaySequence()` - 播放序列
- `TriggerEmotion()` - 触发情绪

### 3. **参数简化**
- 减少了冗余的数据结构
- 统一使用组合动作，避免重复定义
- 简化了配置参数

## 🚀 **使用示例**

### 1. **基础控制 - 单耳动作**

```cpp
// 左耳向前摆动800ms
ear_action_param_t action = {EAR_ACTION_FORWARD, 800};
controller->MoveEar(true, action);

// 右耳向后摆动600ms
ear_action_param_t action2 = {EAR_ACTION_BACKWARD, 600};
controller->MoveEar(false, action2);

// 停止左耳
controller->StopEar(true);

// 停止所有耳朵
controller->StopBoth();
```

### 2. **组合控制 - 双耳动作**

```cpp
// 双耳同时向前
ear_combo_param_t combo = {EAR_COMBO_BOTH_FORWARD, 500};
controller->MoveBoth(combo);

// 左耳向前，右耳保持
ear_combo_param_t combo2 = {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD, 400};
controller->MoveBoth(combo2);

// 交叉动作：左耳向前，右耳向后
ear_combo_param_t combo3 = {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, 300};
controller->MoveBoth(combo3);
```

### 3. **位置控制 - 精确定位**

```cpp
// 左耳竖起
controller->SetEarPosition(true, EAR_POSITION_UP);

// 右耳到中间位置
controller->SetEarPosition(false, EAR_POSITION_MIDDLE);

// 获取左耳当前位置
ear_position_t pos = controller->GetEarPosition(true);

// 重置到默认下垂位置
controller->ResetToDefault();
```

### 4. **序列控制 - 复杂动作**

```cpp
// 定义动作序列
ear_sequence_step_t sequence[] = {
    {EAR_COMBO_BOTH_FORWARD, 500, 200},    // 双耳向前500ms，间隔200ms
    {EAR_COMBO_BOTH_BACKWARD, 500, 200},   // 双耳向后500ms，间隔200ms
    {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD, 300, 100}, // 左耳向前300ms，间隔100ms
    {EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD, 300, 0}    // 右耳向前300ms，无间隔
};

// 播放序列（不循环）
controller->PlaySequence(sequence, 4, false);

// 停止序列
controller->StopSequence();
```

### 5. **情绪控制 - 高级应用**

```cpp
// 设置"开心"情绪的动作序列
ear_sequence_step_t happy_sequence[] = {
    {EAR_COMBO_BOTH_FORWARD, 400, 100},
    {EAR_COMBO_BOTH_BACKWARD, 300, 100},
    {EAR_COMBO_BOTH_FORWARD, 200, 0}
};
controller->SetEmotion("happy", happy_sequence, 3);

// 设置"好奇"情绪的动作序列
ear_sequence_step_t curious_sequence[] = {
    {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD, 800, 400},
    {EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD, 800, 400}
};
controller->SetEmotion("curious", curious_sequence, 2);

// 触发情绪
controller->TriggerEmotion("happy");
controller->TriggerEmotion("curious");

// 停止情绪动作
controller->StopEmotion();
```

## 📊 **接口对比**

### **旧接口（复杂）**
```cpp
// 需要理解多个概念
ear_basic_action_t action = {EAR_ACTION_FORWARD, 800};
controller->ExecuteAction(true, action);

ear_combo_action_data_t combo = {EAR_COMBO_BOTH_FORWARD, 500};
controller->ExecuteComboActionTimed(combo, 500);

ear_action_step_t steps[] = {...};
controller->PlayCustomScenario(steps, 4, true);
```

### **新接口（简洁）**
```cpp
// 概念清晰，命名直观
ear_action_param_t action = {EAR_ACTION_FORWARD, 800};
controller->MoveEar(true, action);

ear_combo_param_t combo = {EAR_COMBO_BOTH_FORWARD, 500};
controller->MoveBoth(combo);

ear_sequence_step_t sequence[] = {...};
controller->PlaySequence(sequence, 4, true);
```

## 🎨 **设计优势**

### 1. **学习成本低**
- 接口命名直观，一看就懂
- 概念层次清晰，循序渐进
- 减少了需要理解的数据结构

### 2. **使用简单**
- 基础功能：`MoveEar()`, `MoveBoth()`
- 高级功能：`PlaySequence()`, `TriggerEmotion()`
- 参数配置简单明了

### 3. **扩展性好**
- 可以轻松添加新的组合动作
- 支持自定义动作序列
- 情绪映射灵活可配置

### 4. **维护性强**
- 代码结构清晰
- 职责分离明确
- 易于调试和修改

## 🔧 **最佳实践**

### 1. **从简单开始**
```cpp
// 先掌握基础控制
controller->MoveEar(true, {EAR_ACTION_FORWARD, 500});

// 再学习组合控制
controller->MoveBoth({EAR_COMBO_BOTH_FORWARD, 500});

// 最后使用高级功能
controller->PlaySequence(sequence, count, false);
```

### 2. **合理使用组合动作**
- 对称动作：`EAR_COMBO_BOTH_FORWARD` - 最常用
- 单耳动作：`EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD` - 表达好奇
- 交叉动作：`EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD` - 表达困惑

### 3. **优化动作序列**
- 动作间隔不要太短（建议100ms以上）
- 动作持续时间要合理（200-1000ms）
- 避免过于复杂的序列（建议不超过10步）

通过这种简化的设计，用户可以更容易地理解和使用耳朵控制系统，同时保持了系统的强大功能和扩展性。
