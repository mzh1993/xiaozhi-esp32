# 耳朵控制延时参数配置说明

## 概述
本文档说明了如何通过调整宏定义来匹配不同的电机，实现精确的耳朵位置控制。

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

### 2. 速度控制延时
```cpp
#define SPEED_SLOW_DELAY_MS            50      // 慢速延时
#define SPEED_NORMAL_DELAY_MS          20      // 正常速度延时
#define SPEED_FAST_DELAY_MS            10      // 快速延时
#define SPEED_VERY_FAST_DELAY_MS       5       // 极快延时
```

**调整建议：**
- 如果动作太快：**增加** 对应速度的延时值
- 如果动作太慢：**减少** 对应速度的延时值

### 3. 场景动作延时
```cpp
#define SCENARIO_DEFAULT_DELAY_MS      150     // 场景步骤间默认延时
#define SCENARIO_LOOP_DELAY_MS         300     // 场景循环间延时
#define EMOTION_COOLDOWN_MS            3000    // 情绪触发冷却时间
```

**调整建议：**
- 如果场景切换太快：**增加** `SCENARIO_DEFAULT_DELAY_MS`
- 如果循环间隔太短：**增加** `SCENARIO_LOOP_DELAY_MS`
- 如果情绪触发太频繁：**增加** `EMOTION_COOLDOWN_MS`

### 4. 特殊动作延时
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

## 电机匹配调整步骤

### 步骤1：基础位置校准
1. 调整 `EAR_POSITION_DOWN_TIME_MS` 使耳朵完全下垂
2. 调整 `EAR_POSITION_UP_TIME_MS` 使耳朵完全竖起
3. 调整 `EAR_POSITION_MIDDLE_TIME_MS` 使耳朵能准确到达中间位置

### 步骤2：速度调优
1. 测试不同速度下的动作效果
2. 根据实际效果调整对应的延时参数
3. 确保动作既不会太快也不会太慢

### 步骤3：场景动作优化
1. 测试各种场景模式的动作效果
2. 调整场景相关的延时参数
3. 确保动作看起来自然流畅

## 常见问题及解决方案

### 问题1：耳朵位置不准确
**原因：** 延时参数与实际电机响应时间不匹配
**解决：** 逐步调整位置控制延时参数，每次增加/减少50-100ms

### 问题2：动作太快或太慢
**原因：** 速度控制延时参数不合适
**解决：** 调整对应速度的延时参数

### 问题3：场景动作不自然
**原因：** 场景延时参数设置不当
**解决：** 调整场景步骤间延时和循环延时

## 注意事项

1. **逐步调整：** 每次只调整一个参数，观察效果后再调整下一个
2. **记录参数：** 记录调整前后的参数值，便于对比和回退
3. **测试验证：** 每次调整后都要测试各种场景，确保整体效果良好
4. **备份配置：** 调整完成后，备份当前的宏定义配置

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

通过合理调整这些参数，您可以让耳朵控制系统完美匹配您的电机硬件！
