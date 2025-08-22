# 耳朵控制延时参数配置说明

## 概述
本文档说明了如何通过调整宏定义来匹配不同的电机，实现精确的耳朵位置控制。新的架构设计完全移除了速度控制概念，只通过时间控制来实现位置定位。

## 架构设计理念

### 1. 基础动作控制
- **只定义物理动作**：前进、后退、停止、刹车
- **通过时间控制位置**：运行指定时间到达目标位置
- **无速度控制**：避免PWM带来的抖动和硬件风险

### 2. 职责分离
- **基础控制层**：只负责GPIO控制和基础动作执行
- **位置控制层**：基于时间的位置定位
- **高级应用层**：用户自定义的场景和情绪映射

## 关键延时参数

### 1. 耳朵位置控制延时（最重要）
```cpp
#define EAR_POSITION_DOWN_TIME_MS      800     // 耳朵下垂所需时间
#define EAR_POSITION_UP_TIME_MS        800     // 耳朵竖起所需时间  
#define EAR_POSITION_MIDDLE_TIME_MS    400     // 耳朵到中间位置所需时间
```

**调整建议：**
- 如果耳朵下垂不到位：**增加** `EAR_POSITION_DOWN_TIME_MS` 的值
- 如果耳朵竖起不到位：**增加** `EAR_POSITION_UP_TIME_MS` 的值
- 如果中间位置不准确：**调整** `EAR_POSITION_MIDDLE_TIME_MS` 的值

### 2. 场景动作延时
```cpp
#define SCENARIO_DEFAULT_DELAY_MS      150     // 场景步骤间默认延时
#define SCENARIO_LOOP_DELAY_MS         300     // 场景循环间延时
#define EMOTION_COOLDOWN_MS            3000    // 情绪触发冷却时间
```

**调整建议：**
- 如果场景切换太快：**增加** `SCENARIO_DEFAULT_DELAY_MS`
- 如果循环间隔太短：**增加** `SCENARIO_LOOP_DELAY_MS`
- 如果情绪触发太频繁：**增加** `EMOTION_COOLDOWN_MS`

### 3. 特殊动作延时
```cpp
#define PEEKABOO_DURATION_MS           2000    // 躲猫猫模式持续时间
#define INSECT_BITE_STEP_TIME_MS       150     // 蚊虫叮咬单步时间
#define INSECT_BITE_DELAY_MS           100     // 蚊虫叮咬步骤间延时
#define CURIOUS_STEP_TIME_MS           800     // 好奇模式单步时间
#define CURIOUS_DELAY_MS               400     // 好奇模式步骤间延时
#define EXCITED_STEP_TIME_MS           300     // 兴奋模式单步时间
#define EXCITED_DELAY_MS               200     // 兴奋模式步骤间延时
#define PLAYFUL_STEP_TIME_MS           600     // 玩耍模式单步时间
#define PLAYFUL_DELAY_MS               300     // 玩耍模式步骤间延时
```

## 为什么移除速度控制

### 1. 直流电机的物理特性
- 直流电机通电即运行，断电即停止
- 没有"速度控制"，只有"启停控制"
- 任何PWM都会导致机械抖动

### 2. PWM控制的问题
- **机械抖动**：电机反复启停，耳朵会剧烈抖动
- **用户体验差**：动作看起来不自然，像故障一样
- **硬件风险**：MOS管频繁开关，产生电流毛刺
- **定位精度差**：无法精确控制位置

### 3. 时间控制的优势
- **精确位置控制**：通过运行时间精确定位
- **硬件安全**：避免频繁开关，保护MOS管
- **用户体验好**：动作流畅自然
- **简单可靠**：逻辑清晰，易于调试

## 电机匹配调整步骤

### 步骤1：基础位置校准
1. 调整 `EAR_POSITION_DOWN_TIME_MS` 使耳朵完全下垂
2. 调整 `EAR_POSITION_UP_TIME_MS` 使耳朵完全竖起
3. 调整 `EAR_POSITION_MIDDLE_TIME_MS` 使耳朵能准确到达中间位置

### 步骤2：场景动作优化
1. 测试各种场景模式的动作效果
2. 调整场景相关的延时参数
3. 确保动作看起来自然流畅

## 常见问题及解决方案

### 问题1：耳朵位置不准确
**原因：** 延时参数与实际电机响应时间不匹配
**解决：** 逐步调整位置控制延时参数，每次增加/减少50-100ms

### 问题2：场景动作不自然
**原因：** 场景延时参数设置不当
**解决：** 调整场景步骤间延时和循环延时

### 问题3：动作太快或太慢
**原因：** 动作持续时间设置不当
**解决：** 调整对应动作的持续时间参数

## 注意事项

1. **逐步调整：** 每次只调整一个参数，观察效果后再调整下一个
2. **记录参数：** 记录调整前后的参数值，便于对比和回退
3. **测试验证：** 每次调整后都要测试各种场景，确保整体效果良好
4. **备份配置：** 调整完成后，备份当前的宏定义配置
5. **避免PWM：** 不要尝试实现速度控制，这会导致硬件问题

## 示例配置

### 快速电机配置
```cpp
#define EAR_POSITION_DOWN_TIME_MS      600     // 减少延时
#define EAR_POSITION_UP_TIME_MS        600     // 减少延时
#define EAR_POSITION_MIDDLE_TIME_MS    300     // 减少延时
```

### 慢速电机配置
```cpp
#define EAR_POSITION_DOWN_TIME_MS      1000    // 增加延时
#define EAR_POSITION_UP_TIME_MS        1000    // 增加延时
#define EAR_POSITION_MIDDLE_TIME_MS    500     // 增加延时
```

### 高精度配置
```cpp
#define EAR_POSITION_DOWN_TIME_MS      900     // 稍微增加延时确保到位
#define EAR_POSITION_UP_TIME_MS        900     // 稍微增加延时确保到位
#define EAR_POSITION_MIDDLE_TIME_MS    450     // 精确的中间位置
```

## 架构优势总结

通过这种设计，我们实现了：

1. **清晰的职责分离**：基础控制、位置控制、高级应用各司其职
2. **硬件安全**：避免PWM带来的硬件风险
3. **用户体验好**：动作流畅自然，无抖动
4. **易于维护**：逻辑清晰，参数可调
5. **扩展性强**：用户可自定义复杂场景

通过合理调整这些参数，您可以让耳朵控制系统完美匹配您的电机硬件，同时确保系统的安全性和可靠性！
