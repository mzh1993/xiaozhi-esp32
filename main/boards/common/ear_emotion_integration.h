#ifndef _EAR_EMOTION_INTEGRATION_H_
#define _EAR_EMOTION_INTEGRATION_H_

#include <string>
#include <map>
#include "ear_controller.h"

#ifdef __cplusplus
extern "C" {
#endif

// 情绪到耳朵动作的映射结构
typedef struct {
    ear_scenario_t ear_scenario;
    uint32_t duration_ms;
    bool auto_stop;
} emotion_ear_mapping_t;

// 函数声明
esp_err_t ear_emotion_integration_init(void);
esp_err_t ear_emotion_integration_deinit(void);

// 根据情绪字符串触发对应的耳朵动作
esp_err_t ear_trigger_by_emotion(const char* emotion);

// 设置自定义情绪映射
esp_err_t ear_set_emotion_mapping(const char* emotion, ear_scenario_t scenario, uint32_t duration_ms);

// 获取当前情绪对应的耳朵动作
emotion_ear_mapping_t* ear_get_emotion_mapping(const char* emotion);

// 停止当前情绪相关的耳朵动作
esp_err_t ear_stop_emotion_action(void);

#ifdef __cplusplus
}
#endif

#endif // _EAR_EMOTION_INTEGRATION_H_
