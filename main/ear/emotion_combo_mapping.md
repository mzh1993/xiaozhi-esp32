# 双耳组合动作到情绪高级动作映射

## 概述
本文档定义了如何将双耳组合动作映射到不同的情绪表达，实现更自然、更丰富的耳朵动作表现。

## 双耳组合动作分类

### 1. 对称动作（Symmetric Actions）
- **双耳向前** (`EAR_COMBO_BOTH_FORWARD`)：表示警觉、好奇、兴奋
- **双耳向后** (`EAR_COMBO_BOTH_BACKWARD`)：表示放松、困倦、伤心
- **双耳停止** (`EAR_COMBO_BOTH_STOP`)：表示平静、专注
- **双耳刹车** (`EAR_COMBO_BOTH_BRAKE`)：表示突然停止、惊讶

### 2. 单耳动作（Single Ear Actions）
- **左耳向前，右耳保持** (`EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD`)：表示左偏好奇
- **左耳向后，右耳保持** (`EAR_COMBO_LEFT_BACKWARD_RIGHT_HOLD`)：表示左偏放松
- **左耳保持，右耳向前** (`EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD`)：表示右偏好奇
- **左耳保持，右耳向后** (`EAR_COMBO_LEFT_HOLD_RIGHT_BACKWARD`)：表示右偏放松

### 3. 交叉动作（Cross Actions）
- **左耳向前，右耳向后** (`EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD`)：表示困惑、思考
- **左耳向后，右耳向前** (`EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD`)：表示困惑、思考（反向）

### 4. 交替动作（Alternating Actions）
- **左耳向前，右耳停止** (`EAR_COMBO_LEFT_FORWARD_RIGHT_STOP`)：表示渐进式好奇
- **左耳向后，右耳停止** (`EAR_COMBO_LEFT_BACKWARD_RIGHT_STOP`)：表示渐进式放松
- **左耳停止，右耳向前** (`EAR_COMBO_LEFT_STOP_RIGHT_FORWARD`)：表示渐进式好奇（反向）
- **左耳停止，右耳向后** (`EAR_COMBO_LEFT_STOP_RIGHT_BACKWARD`)：表示渐进式放松（反向）

## 情绪映射表

### 基础情绪
| 情绪 | 主要组合动作 | 持续时间 | 循环次数 | 说明 |
|------|-------------|----------|----------|------|
| **happy** | `EAR_COMBO_BOTH_FORWARD` | 400ms | 1 | 双耳同时向前，表示开心 |
| **sad** | `EAR_COMBO_BOTH_BACKWARD` | 600ms | 1 | 双耳同时向后，表示伤心 |
| **excited** | `EAR_COMBO_BOTH_FORWARD` | 300ms | 2 | 双耳快速向前，表示兴奋 |
| **sleepy** | `EAR_COMBO_BOTH_BACKWARD` | 800ms | 1 | 双耳缓慢向后，表示困倦 |
| **surprised** | `EAR_COMBO_BOTH_FORWARD` → `EAR_COMBO_BOTH_BACKWARD` | 300ms + 600ms | 1 | 先向前再向后，表示惊讶 |

### 复杂情绪
| 情绪 | 动作序列 | 说明 |
|------|----------|------|
| **curious** | `EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD` → `EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD` | 交替单耳向前，表示好奇探索 |
| **confused** | `EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD` → `EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD` | 交叉动作，表示困惑思考 |
| **playful** | `EAR_COMBO_BOTH_FORWARD` → `EAR_COMBO_BOTH_BACKWARD` → `EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD` | 复杂序列，表示玩耍 |
| **gentle** | `EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD` → `EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD` | 缓慢交替，表示温和 |

### 特殊情绪
| 情绪 | 动作序列 | 说明 |
|------|----------|------|
| **alert** | `EAR_COMBO_BOTH_FORWARD` | 双耳竖起，表示警觉 |
| **relaxed** | `EAR_COMBO_BOTH_BACKWARD` | 双耳下垂，表示放松 |
| **focused** | `EAR_COMBO_BOTH_STOP` | 双耳停止，表示专注 |
| **startled** | `EAR_COMBO_BOTH_BRAKE` → `EAR_COMBO_BOTH_FORWARD` | 先刹车再向前，表示受惊 |

## 实现建议

### 1. 动作持续时间
- **快速动作**：200-400ms，适合兴奋、惊讶等情绪
- **正常动作**：400-800ms，适合开心、好奇等情绪
- **缓慢动作**：800-1200ms，适合困倦、放松等情绪

### 2. 动作间隔
- **连续动作**：0ms间隔，动作流畅
- **短间隔**：100-200ms间隔，动作清晰
- **长间隔**：300-500ms间隔，动作从容

### 3. 循环控制
- **单次执行**：loop=false，适合一次性情绪表达
- **多次循环**：loop=true，适合持续情绪状态
- **循环次数**：1-3次，避免过度重复

## 使用示例

### 基础使用
```cpp
// 开心情绪
ear_combo_action_data_t happy_action = {EAR_COMBO_BOTH_FORWARD, 400};
controller->ExecuteComboActionTimed(happy_action, 400);

// 伤心情绪
ear_combo_action_data_t sad_action = {EAR_COMBO_BOTH_BACKWARD, 600};
controller->ExecuteComboActionTimed(sad_action, 600);
```

### 复杂序列
```cpp
// 好奇情绪序列
ear_action_step_t curious_steps[] = {
    {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD, 800, 400},
    {EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD, 800, 400},
    {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD, 600, 300},
    {EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD, 600, 300}
};
controller->PlayCustomScenario(curious_steps, 4, true);
```

## 注意事项

1. **动作协调性**：确保双耳动作协调，避免不自然的动作
2. **时间控制**：精确控制动作时间，实现准确的位置定位
3. **情绪一致性**：动作要与情绪表达保持一致
4. **用户反馈**：根据用户反馈调整动作参数
5. **硬件保护**：避免过于频繁的动作切换，保护电机硬件

通过这种映射设计，我们可以实现更加丰富和自然的耳朵情绪表达，提升用户体验。
