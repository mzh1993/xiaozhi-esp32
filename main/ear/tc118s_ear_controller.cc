#include "tc118s_ear_controller.h"
#include "application.h"
#include <esp_log.h>
#include <esp_err.h>
#include "esp_timer.h"
#include <string.h>
#include <cinttypes>
#include <memory>

static const char *TAG = "TC118S_EAR_CONTROLLER";

// 日志控制宏：屏蔽详细调试日志，专注于情绪转换观察
// 设置为 0 可屏蔽详细日志，设置为 1 显示所有日志
#ifndef ENABLE_VERBOSE_EAR_LOGS
#define ENABLE_VERBOSE_EAR_LOGS 0
#endif

// 宏定义：条件编译详细日志
#if ENABLE_VERBOSE_EAR_LOGS
#define EAR_LOG_VERBOSE(...) ESP_LOGI(__VA_ARGS__)
#define EAR_LOG_DEBUG(...) ESP_LOGD(__VA_ARGS__)
#define EAR_LOG_DURATION(...) ESP_LOGI(__VA_ARGS__)
#else
#define EAR_LOG_VERBOSE(...) ((void)0)
#define EAR_LOG_DEBUG(...) ((void)0)
#define EAR_LOG_DURATION(...) ((void)0)
#endif

// Ensure FreeRTOS timers never get a 0-tick period when ms < tick resolution
#ifndef MS_TO_TICKS_MIN1
#define MS_TO_TICKS_MIN1(ms) ({ TickType_t __t = pdMS_TO_TICKS(ms); (__t == 0 ? (TickType_t)1 : __t); })
#endif

// 重新设计的情绪序列 - 基于三个基础参数（120ms/120ms/60ms）简化设计
// 方向映射：FORWARD(0)=朝向DOWN(下垂), BACKWARD(1)=朝向UP(竖起)
const ear_sequence_step_t Tc118sEarController::happy_sequence_[] = {
    // 一般高兴：摇摆2-3次，幅度大，最后停留在竖起位置（UP）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,  EAR_PAUSE_MEDIUM_MS},  // 完全竖起（UP，120ms）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_MID_MS,   EAR_PAUSE_MEDIUM_MS},  // 中幅下垂（DOWN，60ms）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,  EAR_PAUSE_NONE_MS}     // 完全竖起并停留（UP，120ms）
};

const ear_sequence_step_t Tc118sEarController::curious_sequence_[] = {
    // 好奇：使用交叉动作，左右耳相反方向运动，模拟"左顾右盼"，最后竖起（UP）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_MEDIUM_MS},  // 左耳下垂，右耳竖起
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_MEDIUM_MS},  // 左耳竖起，右耳下垂（反向交叉）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_MEDIUM_MS},  // 左耳下垂，右耳竖起
    {EAR_COMBO_BOTH_BACKWARD,               EAR_MOVE_FULL_MS, EAR_PAUSE_NONE_MS}     // 最后双耳竖起（UP）
};

const ear_sequence_step_t Tc118sEarController::excited_sequence_[] = {
    // 超级兴奋：短时间前后摇摆5-6次，幅度小强度大，加入交叉动作增加动感，最后竖起（UP）
    {EAR_COMBO_BOTH_BACKWARD,                 EAR_MOVE_SMALL_MS, EAR_PAUSE_SHORT_MS},   // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,                  EAR_MOVE_SMALL_MS, EAR_PAUSE_SHORT_MS},   // 小幅下垂（DOWN）
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD,   EAR_MOVE_SMALL_MS, EAR_PAUSE_SHORT_MS},   // 左耳竖起，右耳下垂（交叉动作）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD,   EAR_MOVE_SMALL_MS, EAR_PAUSE_SHORT_MS},   // 左耳下垂，右耳竖起（反向交叉）
    {EAR_COMBO_BOTH_BACKWARD,                 EAR_MOVE_SMALL_MS, EAR_PAUSE_SHORT_MS},   // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,                  EAR_MOVE_SMALL_MS, EAR_PAUSE_SHORT_MS},   // 小幅下垂（DOWN）
    {EAR_COMBO_BOTH_BACKWARD,                 EAR_MOVE_MID_MS,   EAR_PAUSE_NONE_MS}     // 中幅竖起并停留（UP）
};

const ear_sequence_step_t Tc118sEarController::playful_sequence_[] = {
    // 顽皮：快速变化，加入交叉动作增加趣味，最后竖起（UP）
    {EAR_COMBO_BOTH_BACKWARD,                 EAR_MOVE_MID_MS,    EAR_PAUSE_MEDIUM_MS},  // 竖起（UP）
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD,   EAR_MOVE_MID_MS,    EAR_PAUSE_SHORT_MS},   // 左耳竖起，右耳下垂（交叉动作）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD,   EAR_MOVE_MID_MS,    EAR_PAUSE_SHORT_MS},   // 左耳下垂，右耳竖起（反向交叉）
    {EAR_COMBO_BOTH_BACKWARD,                 EAR_MOVE_MID_MS,    EAR_PAUSE_NONE_MS}     // 竖起并停留（UP）
};

const ear_sequence_step_t Tc118sEarController::sad_sequence_[] = {
    // 悲伤：缓慢的2-3次摆动，下垂幅度大（盖住眼睛），抬起幅度小，使用长停顿表达消极情绪，最后停留在下摆（DOWN）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_VERY_LONG_MS},  // 完全下垂（DOWN，盖住眼睛，120ms），长停顿
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_SMALL_MS,  EAR_PAUSE_VERY_LONG_MS},  // 小幅抬起（UP，30ms），长停顿
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_NONE_MS}       // 最后停留在下摆位置（DOWN，120ms）
};

const ear_sequence_step_t Tc118sEarController::surprised_sequence_[] = {
    // 惊讶：快速竖起（UP），然后缓慢下垂（DOWN），表达"震惊"到"恢复"，最后回到中间
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_LONG_MS},   // 快速完全竖起（UP，120ms）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_MEDIUM_MS}, // 缓慢下垂（DOWN，120ms）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_MID_MS,    EAR_PAUSE_NONE_MS}    // 回到中间位置（UP方向，60ms）
};

const ear_sequence_step_t Tc118sEarController::sleepy_sequence_[] = {
    // 困倦：非常缓慢下垂，表达疲惫，使用长停顿，最后停留在下摆位置（DOWN）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_VERY_LONG_MS},  // 完全下垂（DOWN，120ms），非常长停顿
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_NONE_MS}        // 最后停留在下摆位置（DOWN，120ms）
};

const ear_sequence_step_t Tc118sEarController::confident_sequence_[] = {
    // 自信：稳定节奏，幅度中等，表达坚定，最后停留在竖起位置（UP）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_LONG_MS},  // 完全竖起（UP，120ms）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_MID_MS,    EAR_PAUSE_LONG_MS},  // 中幅下垂（DOWN，60ms）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_LONG_MS},  // 完全竖起（UP，120ms）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_NONE_MS}   // 停留在竖起位置（UP，120ms）
};

const ear_sequence_step_t Tc118sEarController::confused_sequence_[] = {
    // 困惑：不规则节奏，使用正反向交叉动作，表达混乱，最后统一竖起（UP）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_MID_MS,    EAR_PAUSE_MEDIUM_MS},  // 左耳下垂，右耳竖起
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD, EAR_MOVE_MID_MS,    EAR_PAUSE_MEDIUM_MS},  // 左耳竖起，右耳下垂（反向）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},   // 快速交叉
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD, EAR_MOVE_MID_MS,    EAR_PAUSE_MEDIUM_MS},  // 反向交叉
    {EAR_COMBO_BOTH_BACKWARD,               EAR_MOVE_MID_MS,    EAR_PAUSE_NONE_MS}     // 最后统一竖起（UP）
};

const ear_sequence_step_t Tc118sEarController::loving_sequence_[] = {
    // 爱意：温柔节奏，幅度中等，表达温柔，最后停留在竖起位置（UP）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_MID_MS,    EAR_PAUSE_LONG_MS},  // 竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_MID_MS,    EAR_PAUSE_LONG_MS},  // 下垂（DOWN）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_MID_MS,    EAR_PAUSE_LONG_MS},  // 竖起（UP）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_MID_MS,    EAR_PAUSE_NONE_MS}   // 停留在竖起位置（UP）
};

const ear_sequence_step_t Tc118sEarController::angry_sequence_[] = {
    // 愤怒：快速节奏，小幅快速摆动，表达激烈，最后停留在中间偏下位置（DOWN）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅下垂（DOWN）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅下垂（DOWN）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_MID_MS,    EAR_PAUSE_NONE_MS}    // 中幅下垂并停留（DOWN）
};

const ear_sequence_step_t Tc118sEarController::cool_sequence_[] = {
    // 酷：慢速节奏，幅度大，表达冷静，最后停留在竖起位置（UP）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_LONG_MS},  // 完全竖起（UP，120ms）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_MID_MS,    EAR_PAUSE_LONG_MS},  // 中幅下垂（DOWN，60ms）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_LONG_MS},  // 完全竖起（UP，120ms）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_NONE_MS}   // 停留在竖起位置（UP，120ms）
};

// 新增：不同强度情绪序列 - 基于基础序列优化
const ear_sequence_step_t Tc118sEarController::crying_sequence_[] = {
    // 哭泣：比悲伤更强烈，但保持简洁，使用长停顿表达消极情绪，最后停留在下摆位置（DOWN）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_VERY_LONG_MS},  // 完全下垂（DOWN，120ms），非常长停顿
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_SMALL_MS,  EAR_PAUSE_VERY_LONG_MS},  // 小幅抬起（UP，30ms），非常长停顿
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_NONE_MS}       // 最后停留在下摆位置（DOWN，120ms）
};

const ear_sequence_step_t Tc118sEarController::furious_sequence_[] = {
    // 狂怒：比愤怒更激烈，更多次快速摆动，加入交叉动作，最后停留在中间偏下位置（DOWN）
    {EAR_COMBO_BOTH_BACKWARD,                 EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,                  EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅下垂（DOWN）
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD,   EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 左耳竖起，右耳下垂（交叉动作）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD,   EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 左耳下垂，右耳竖起（反向交叉）
    {EAR_COMBO_BOTH_BACKWARD,                 EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,                  EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅下垂（DOWN）
    {EAR_COMBO_BOTH_BACKWARD,                 EAR_MOVE_SMALL_MS,  EAR_PAUSE_SHORT_MS},  // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,                  EAR_MOVE_MID_MS,    EAR_PAUSE_NONE_MS}    // 中幅下垂并停留（DOWN）
};

const ear_sequence_step_t Tc118sEarController::shocked_sequence_[] = {
    // 震惊：比惊讶更强烈，快速竖起并保持更久，然后缓慢恢复，最后回到中间位置
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_LONG_MS},  // 快速完全竖起（UP，120ms）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS,   EAR_PAUSE_LONG_MS},  // 保持竖起（UP，增加一次，更强烈）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_MEDIUM_MS}, // 缓慢下垂（DOWN，120ms）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_MID_MS,    EAR_PAUSE_NONE_MS}    // 回到中间位置（UP方向，60ms）
};

const ear_sequence_step_t Tc118sEarController::annoyed_sequence_[] = {
    // 烦恼：比愤怒更温和，摆动次数更少，节奏稍慢，最后停留在中间偏下位置（DOWN）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_SMALL_MS,  EAR_PAUSE_MEDIUM_MS},  // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_SMALL_MS,  EAR_PAUSE_MEDIUM_MS},  // 小幅下垂（DOWN）
    {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_SMALL_MS,  EAR_PAUSE_MEDIUM_MS},  // 小幅竖起（UP）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_SMALL_MS,  EAR_PAUSE_MEDIUM_MS},  // 小幅下垂（DOWN）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_MID_MS,    EAR_PAUSE_NONE_MS}     // 中幅下垂并停留（DOWN）
};

const ear_sequence_step_t Tc118sEarController::embarrassed_sequence_[] = {
    // 尴尬：缓慢下垂，表达害羞，使用长停顿，最后停留在下摆位置（DOWN）
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_VERY_LONG_MS},  // 缓慢下垂（DOWN，120ms），非常长停顿
    {EAR_COMBO_BOTH_FORWARD,  EAR_MOVE_FULL_MS,   EAR_PAUSE_NONE_MS}       // 最后停留在下摆位置（DOWN，120ms）
};

const ear_sequence_step_t Tc118sEarController::thinking_sequence_[] = {
    // 思考：比好奇更慢更稳定，左右交替更慢，表达深思，最后双耳竖起（UP）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_LONG_MS},  // 左耳下垂，右耳竖起，更慢
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_LONG_MS},  // 左耳竖起，右耳下垂，更慢
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_LONG_MS},  // 左耳下垂，右耳竖起，更慢
    {EAR_COMBO_BOTH_BACKWARD,               EAR_MOVE_FULL_MS, EAR_PAUSE_NONE_MS}   // 最后双耳竖起（UP）
};

const ear_sequence_step_t Tc118sEarController::listening_sequence_[] = {
    // 倾听：比好奇更专注，单耳交替竖起，节奏适中，最后双耳竖起（UP）
    // 使用交叉动作实现单耳交替竖起效果：一只耳朵竖起（UP），另一只下垂（DOWN）
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_MEDIUM_MS},  // 左耳竖起（UP），右耳下垂（DOWN）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_MEDIUM_MS},  // 左耳下垂（DOWN），右耳竖起（UP）
    {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_MEDIUM_MS},  // 左耳竖起（UP），右耳下垂（DOWN，重复）
    {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_FULL_MS, EAR_PAUSE_MEDIUM_MS},  // 左耳下垂（DOWN），右耳竖起（UP，重复）
    {EAR_COMBO_BOTH_BACKWARD,               EAR_MOVE_FULL_MS, EAR_PAUSE_NONE_MS}     // 最后双耳竖起（UP）
};

// 默认情绪映射 - 基于时间控制的情绪表达
const std::map<std::string, std::vector<ear_sequence_step_t>> Tc118sEarController::default_emotion_mappings_ = {
    // ===== 中性/无动作情绪 =====
    {"neutral", {}},      // 中性：无动作
    {"relaxed", {}},      // 放松：无动作
    
    // ===== 开心类情绪 - 快速节奏 =====
    {"happy", {std::vector<ear_sequence_step_t>(happy_sequence_, happy_sequence_ + sizeof(happy_sequence_)/sizeof(happy_sequence_[0]))}},
    {"joyful", {std::vector<ear_sequence_step_t>(happy_sequence_, happy_sequence_ + sizeof(happy_sequence_)/sizeof(happy_sequence_[0]))}},  // 快乐：用开心序列
    {"cheerful", {std::vector<ear_sequence_step_t>(happy_sequence_, happy_sequence_ + sizeof(happy_sequence_)/sizeof(happy_sequence_[0]))}}, // 愉快：用开心序列
    
    // ===== 兴奋类情绪 - 超快速节奏 =====
    {"excited", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}},
    {"laughing", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}},  // 大笑：用兴奋序列
    {"delicious", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}}, // 美味：用兴奋序列
    {"thrilled", {std::vector<ear_sequence_step_t>(excited_sequence_, excited_sequence_ + sizeof(excited_sequence_)/sizeof(excited_sequence_[0]))}}, // 兴奋：用兴奋序列
    
    // ===== 顽皮类情绪 - 节奏变化 =====
    {"playful", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},
    {"funny", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},     // 有趣：用顽皮序列
    {"silly", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + sizeof(playful_sequence_)/sizeof(playful_sequence_[0]))}},    // 傻傻的：用顽皮序列
    {"winking", {std::vector<ear_sequence_step_t>(playful_sequence_, playful_sequence_ + 2)}},  // 眨眼：用顽皮序列前2步
    
    // ===== 悲伤类情绪 - 慢速节奏 =====
    {"sad", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},
    {"crying", {std::vector<ear_sequence_step_t>(crying_sequence_, crying_sequence_ + sizeof(crying_sequence_)/sizeof(crying_sequence_[0]))}},           // 哭泣：使用专门的哭泣序列（更强烈）
    {"embarrassed", {std::vector<ear_sequence_step_t>(embarrassed_sequence_, embarrassed_sequence_ + sizeof(embarrassed_sequence_)/sizeof(embarrassed_sequence_[0]))}},     // 尴尬：使用专门的尴尬序列（短暂快速下垂）
    {"disappointed", {std::vector<ear_sequence_step_t>(sad_sequence_, sad_sequence_ + sizeof(sad_sequence_)/sizeof(sad_sequence_[0]))}},   // 失望：用悲伤序列
    
    // ===== 惊讶类情绪 - 快速到慢速 =====
    {"surprised", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}},
    {"shocked", {std::vector<ear_sequence_step_t>(shocked_sequence_, shocked_sequence_ + sizeof(shocked_sequence_)/sizeof(shocked_sequence_[0]))}}, // 震惊：使用专门的震惊序列（更强烈，保持更久）
    {"amazed", {std::vector<ear_sequence_step_t>(surprised_sequence_, surprised_sequence_ + sizeof(surprised_sequence_)/sizeof(surprised_sequence_[0]))}}, // 惊讶：用惊讶序列
    
    // ===== 愤怒类情绪 - 快速节奏 =====
    {"angry", {std::vector<ear_sequence_step_t>(angry_sequence_, angry_sequence_ + sizeof(angry_sequence_)/sizeof(angry_sequence_[0]))}},
    {"furious", {std::vector<ear_sequence_step_t>(furious_sequence_, furious_sequence_ + sizeof(furious_sequence_)/sizeof(furious_sequence_[0]))}},     // 狂怒：使用专门的狂怒序列（更激烈，有交叉动作）
    {"annoyed", {std::vector<ear_sequence_step_t>(annoyed_sequence_, annoyed_sequence_ + sizeof(annoyed_sequence_)/sizeof(annoyed_sequence_[0]))}},   // 烦恼：使用专门的烦恼序列（更温和）
    
    // ===== 好奇类情绪 - 左右交替 =====
    {"curious", {std::vector<ear_sequence_step_t>(curious_sequence_, curious_sequence_ + sizeof(curious_sequence_)/sizeof(curious_sequence_[0]))}},
    {"thinking", {std::vector<ear_sequence_step_t>(thinking_sequence_, thinking_sequence_ + sizeof(thinking_sequence_)/sizeof(thinking_sequence_[0]))}},  // 思考：使用专门的思考序列（更慢更稳定）
    {"listening", {std::vector<ear_sequence_step_t>(listening_sequence_, listening_sequence_ + sizeof(listening_sequence_)/sizeof(listening_sequence_[0]))}}, // 倾听：使用专门的倾听序列（单耳交替，专注）
    
    // ===== 爱意类情绪 - 温柔节奏 =====
    {"loving", {std::vector<ear_sequence_step_t>(loving_sequence_, loving_sequence_ + sizeof(loving_sequence_)/sizeof(loving_sequence_[0]))}},
    {"kissy", {std::vector<ear_sequence_step_t>(loving_sequence_, loving_sequence_ + sizeof(loving_sequence_)/sizeof(loving_sequence_[0]))}},     // 亲吻：用爱意序列
    {"caring", {std::vector<ear_sequence_step_t>(loving_sequence_, loving_sequence_ + sizeof(loving_sequence_)/sizeof(loving_sequence_[0]))}},   // 关心：用爱意序列
    
    // ===== 自信类情绪 - 稳定节奏 =====
    {"confident", {std::vector<ear_sequence_step_t>(confident_sequence_, confident_sequence_ + sizeof(confident_sequence_)/sizeof(confident_sequence_[0]))}},
    {"proud", {std::vector<ear_sequence_step_t>(confident_sequence_, confident_sequence_ + sizeof(confident_sequence_)/sizeof(confident_sequence_[0]))}}, // 骄傲：用自信序列
    {"determined", {std::vector<ear_sequence_step_t>(confident_sequence_, confident_sequence_ + sizeof(confident_sequence_)/sizeof(confident_sequence_[0]))}}, // 坚定：用自信序列
    
    // ===== 酷类情绪 - 慢速节奏 =====
    {"cool", {std::vector<ear_sequence_step_t>(cool_sequence_, cool_sequence_ + sizeof(cool_sequence_)/sizeof(cool_sequence_[0]))}},
    {"calm", {std::vector<ear_sequence_step_t>(cool_sequence_, cool_sequence_ + sizeof(cool_sequence_)/sizeof(cool_sequence_[0]))}},         // 冷静：用酷序列
    {"chill", {std::vector<ear_sequence_step_t>(cool_sequence_, cool_sequence_ + sizeof(cool_sequence_)/sizeof(cool_sequence_[0]))}},        // 放松：用酷序列
    
    // ===== 困惑类情绪 - 不规则节奏 =====
    {"confused", {std::vector<ear_sequence_step_t>(confused_sequence_, confused_sequence_ + sizeof(confused_sequence_)/sizeof(confused_sequence_[0]))}},
    {"puzzled", {std::vector<ear_sequence_step_t>(confused_sequence_, confused_sequence_ + sizeof(confused_sequence_)/sizeof(confused_sequence_[0]))}}, // 困惑：用困惑序列
    {"lost", {std::vector<ear_sequence_step_t>(confused_sequence_, confused_sequence_ + sizeof(confused_sequence_)/sizeof(confused_sequence_[0]))}},    // 迷失：用困惑序列
    
    // ===== 特殊情绪 - 独特节奏 =====
    {"sleepy", {std::vector<ear_sequence_step_t>(sleepy_sequence_, sleepy_sequence_ + sizeof(sleepy_sequence_)/sizeof(sleepy_sequence_[0]))}},     // 困倦：超慢速节奏
    {"tired", {std::vector<ear_sequence_step_t>(sleepy_sequence_, sleepy_sequence_ + sizeof(sleepy_sequence_)/sizeof(sleepy_sequence_[0]))}},      // 疲惫：用困倦序列
    {"drowsy", {std::vector<ear_sequence_step_t>(sleepy_sequence_, sleepy_sequence_ + sizeof(sleepy_sequence_)/sizeof(sleepy_sequence_[0]))}},     // 昏昏欲睡：用困倦序列
};

Tc118sEarController::Tc118sEarController(gpio_num_t left_ina_pin, gpio_num_t left_inb_pin,
                                       gpio_num_t right_ina_pin, gpio_num_t right_inb_pin)
    : left_ina_pin_(left_ina_pin)
    , left_inb_pin_(left_inb_pin)
    , right_ina_pin_(right_ina_pin)
    , right_inb_pin_(right_inb_pin)
    , current_emotion_("neutral")
    , last_emotion_time_(0)
    , emotion_action_active_(false)
    , stop_timer_(nullptr)
    , current_combo_action_(EAR_COMBO_BOTH_STOP)
    , last_combo_start_time_ms_(0)
    , gpio_set_time_ms_(0)
    , scheduled_duration_ms_(0)
    , stop_timer_scheduled_time_ms_(0) {
    
    ESP_LOGI(TAG, "TC118S Ear Controller created with pins: L_INA=%d, L_INB=%d, R_INA=%d, R_INB=%d",
             left_ina_pin_, left_inb_pin_, right_ina_pin_, right_inb_pin_);
}

Tc118sEarController::~Tc118sEarController() {
    if (initialized_) {
        Deinitialize();
    }
}

esp_err_t Tc118sEarController::Initialize() {
    ESP_LOGI(TAG, "Initializing TC118S ear controller");
    
    // 调用基类初始化
    esp_err_t ret = InitializeBase();
    if (ret != ESP_OK) {
        return ret;
    }

    // 初始化左耳
    left_ear_.ina_pin = left_ina_pin_;
    left_ear_.inb_pin = left_inb_pin_;
    left_ear_.is_left_ear = true;
    left_ear_.current_action = EAR_ACTION_STOP;
    left_ear_.is_active = false;
    
    // 初始化右耳
    right_ear_.ina_pin = right_ina_pin_;
    right_ear_.inb_pin = right_inb_pin_;
    right_ear_.is_left_ear = false;
    right_ear_.current_action = EAR_ACTION_STOP;
    right_ear_.is_active = false;
    
    // 配置GPIO引脚
    gpio_reset_pin(left_ina_pin_);
    gpio_reset_pin(left_inb_pin_);
    gpio_reset_pin(right_ina_pin_);
    gpio_reset_pin(right_inb_pin_);
    
    gpio_set_direction(left_ina_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(left_inb_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(right_ina_pin_, GPIO_MODE_OUTPUT);
    gpio_set_direction(right_inb_pin_, GPIO_MODE_OUTPUT);
    
    // 初始化所有引脚为低电平（停止状态）
    gpio_set_level(left_ina_pin_, 0);
    gpio_set_level(left_inb_pin_, 0);
    gpio_set_level(right_ina_pin_, 0);
    gpio_set_level(right_inb_pin_, 0);
    
    // 初始化默认情绪映射
    InitializeDefaultEmotionMappings();
    
    // 设置序列模式
    SetupSequencePatterns();
    
    // 创建状态互斥锁
    state_mutex_ = xSemaphoreCreateMutex();
    if (!state_mutex_) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建双耳停止定时器
    stop_timer_ = xTimerCreate("EarStopTimer", pdMS_TO_TICKS(100), pdFALSE, this, 
                               [](TimerHandle_t timer) {
                                   Tc118sEarController* controller = static_cast<Tc118sEarController*>(pvTimerGetTimerID(timer));
                                   controller->OnStopTimer(timer);
                               });
    // 创建单耳停止定时器（左/右）
    stop_ctx_left_ = (StopCtx*)pvPortMalloc(sizeof(StopCtx));
    stop_ctx_right_ = (StopCtx*)pvPortMalloc(sizeof(StopCtx));
    if (stop_ctx_left_) { stop_ctx_left_->self = this; stop_ctx_left_->left = true; }
    if (stop_ctx_right_) { stop_ctx_right_->self = this; stop_ctx_right_->left = false; }
    stop_timer_left_ = xTimerCreate("EarStopL", pdMS_TO_TICKS(100), pdFALSE, stop_ctx_left_, 
                               [](TimerHandle_t timer) {
                                   StopCtx* ctx = static_cast<StopCtx*>(pvTimerGetTimerID(timer));
                                   if (ctx && ctx->self) ctx->self->OnSingleStopTimer(timer);
                               });
    stop_timer_right_ = xTimerCreate("EarStopR", pdMS_TO_TICKS(100), pdFALSE, stop_ctx_right_, 
                               [](TimerHandle_t timer) {
                                   StopCtx* ctx = static_cast<StopCtx*>(pvTimerGetTimerID(timer));
                                   if (ctx && ctx->self) ctx->self->OnSingleStopTimer(timer);
                               });
    if (!stop_timer_) {
        ESP_LOGE(TAG, "Failed to create stop timer");
        if (state_mutex_) {
            vSemaphoreDelete(state_mutex_);
            state_mutex_ = nullptr;
        }
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "TC118S ear controller initialized successfully");
    return ESP_OK;
}

esp_err_t Tc118sEarController::Deinitialize() {
    ESP_LOGI(TAG, "Deinitializing TC118S ear controller");
    
    if (!initialized_) {
        return ESP_OK;
    }
    
    // 停止所有序列
    if (sequence_active_) {
        StopSequence();
    }
    
    // 确保耳朵回到下垂状态
    ResetToDefault();
    
    // 等待耳朵动作完成
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 停止所有耳朵
    StopBoth();
    
    // 删除停止定时器 - 先停止再删除，等待完成
    if (stop_timer_) {
        xTimerStop(stop_timer_, portMAX_DELAY);
        xTimerDelete(stop_timer_, portMAX_DELAY);
        stop_timer_ = nullptr;
    }
    if (stop_timer_left_) {
        xTimerStop(stop_timer_left_, portMAX_DELAY);
        xTimerDelete(stop_timer_left_, portMAX_DELAY);
        stop_timer_left_ = nullptr;
    }
    if (stop_timer_right_) {
        xTimerStop(stop_timer_right_, portMAX_DELAY);
        xTimerDelete(stop_timer_right_, portMAX_DELAY);
        stop_timer_right_ = nullptr;
    }
    if (stop_ctx_left_) { vPortFree(stop_ctx_left_); stop_ctx_left_ = nullptr; }
    if (stop_ctx_right_) { vPortFree(stop_ctx_right_); stop_ctx_right_ = nullptr; }
    
    // 删除状态互斥锁
    if (state_mutex_) {
        vSemaphoreDelete(state_mutex_);
        state_mutex_ = nullptr;
    }
    
    // 调用基类反初始化
    return DeinitializeBase();
}

void Tc118sEarController::SetGpioLevels(bool left_ear, ear_action_t action) {
    gpio_num_t ina_pin = left_ear ? left_ina_pin_ : right_ina_pin_;
    gpio_num_t inb_pin = left_ear ? left_inb_pin_ : right_inb_pin_;
    
    switch (action) {
        case EAR_ACTION_STOP:
            gpio_set_level(ina_pin, 0);
            gpio_set_level(inb_pin, 0);
            break;
        case EAR_ACTION_FORWARD:
            // 修改：交换 GPIO 映射，使 FORWARD 朝向 DOWN（下垂）
            gpio_set_level(ina_pin, 0);
            gpio_set_level(inb_pin, 1);
            break;
        case EAR_ACTION_BACKWARD:
            // 修改：交换 GPIO 映射，使 BACKWARD 朝向 UP（竖起）
            gpio_set_level(ina_pin, 1);
            gpio_set_level(inb_pin, 0);
            break;
        case EAR_ACTION_BRAKE:
            gpio_set_level(ina_pin, 1);
            gpio_set_level(inb_pin, 1);
            break;
    }
    
    // 更新状态
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    ear->current_action = action;
    ear->is_active = (action != EAR_ACTION_STOP);
}

// ===== 基础控制接口实现 =====
esp_err_t Tc118sEarController::MoveEar(bool left_ear, ear_action_param_t action) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    if (action.action == EAR_ACTION_STOP) {
        ESP_LOGD(TAG, "Moving %s ear: action=%d, duration=%lu ms", 
                 left_ear ? "left" : "right", action.action, action.duration_ms);
    } else {
        EAR_LOG_VERBOSE(TAG, "Moving %s ear: action=%d, duration=%lu ms", 
                 left_ear ? "left" : "right", action.action, action.duration_ms);
    }
    
    // 设置GPIO状态
    SetGpioLevels(left_ear, action.action);
    
    // 运行指定时间（非阻塞：单耳停止定时器控制停止）
    if (action.duration_ms > 0) {
        TimerHandle_t t = left_ear ? stop_timer_left_ : stop_timer_right_;
        if (t) {
            xTimerStop(t, 0);
            xTimerChangePeriod(t, MS_TO_TICKS_MIN1(action.duration_ms), 0);
            xTimerStart(t, 0);
        } else {
            // 无定时器则直接停止该耳
            SetGpioLevels(left_ear, EAR_ACTION_STOP);
        }
    }
    
    return ESP_OK;
}

esp_err_t Tc118sEarController::StopEar(bool left_ear) {
    ear_action_param_t action = {EAR_ACTION_STOP, 0};
    return MoveEar(left_ear, action);
}

esp_err_t Tc118sEarController::StopBoth() {
    // 记录停止时间并计算实际持续时间（如果之前有动作）
    uint64_t stop_time_ms = esp_timer_get_time() / 1000;
    if (gpio_set_time_ms_ > 0 && scheduled_duration_ms_ > 0) {
        uint64_t actual_duration_ms = stop_time_ms - gpio_set_time_ms_;
        int64_t duration_error_ms = (int64_t)actual_duration_ms - (int64_t)scheduled_duration_ms_;
        
        // 打印持续时间验证信息
        if (duration_error_ms > 5 || duration_error_ms < -5) {
            ESP_LOGW(TAG, "[DURATION] Action duration mismatch: scheduled=%lu ms, actual=%" PRIu64 " ms, error=%" PRId64 " ms (action=%d)", 
                     scheduled_duration_ms_, actual_duration_ms, duration_error_ms, current_combo_action_);
        } else {
            EAR_LOG_DURATION(TAG, "[DURATION] Action duration: scheduled=%lu ms, actual=%" PRIu64 " ms, error=%" PRId64 " ms (action=%d)", 
                     scheduled_duration_ms_, actual_duration_ms, duration_error_ms, current_combo_action_);
        }
        
        // 如果误差超过20%，记录警告
        if (scheduled_duration_ms_ > 0) {
            int32_t error_percent = (int32_t)((duration_error_ms * 100) / scheduled_duration_ms_);
            if (error_percent > 20 || error_percent < -20) {
                ESP_LOGW(TAG, "[DURATION] Large duration error: %d%% (action=%d, scheduled=%lu ms, actual=%" PRIu64 " ms)", 
                         error_percent, current_combo_action_, scheduled_duration_ms_, actual_duration_ms);
            }
        }
    }
    
    // P0修复：检查是否是序列的最后一个步骤
    // 如果是，在GPIO停止后调用MarkSequenceCompleted
    bool is_last_sequence_move = false;
    if (state_mutex_) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        is_last_sequence_move = is_last_sequence_move_;
        xSemaphoreGive(state_mutex_);
    } else {
        is_last_sequence_move = is_last_sequence_move_;
    }
    
    if (stop_timer_) {
        xTimerStop(stop_timer_, 0);
    }
    ResetComboState();
    Application::GetInstance().CancelEarComboStopTimer();
    StopEar(true);
    StopEar(false);
    
    // 重置持续时间监控
    gpio_set_time_ms_ = 0;
    scheduled_duration_ms_ = 0;
    stop_timer_scheduled_time_ms_ = 0;
    
    // P0修复：如果是序列的最后一个步骤，在GPIO停止后调用MarkSequenceCompleted
    // 这样可以确保在GPIO真正停止后才设置完成标志，避免时序竞争
    // 关键修复：只有在序列仍然活跃时才处理最后一个步骤
    bool is_sequence_still_active = false;
    if (state_mutex_) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        is_sequence_still_active = sequence_active_;
        xSemaphoreGive(state_mutex_);
    } else {
        is_sequence_still_active = sequence_active_;
    }
    
    if (is_last_sequence_move && is_sequence_still_active) {
        ESP_LOGI(TAG, "[SEQUENCE] Last sequence step stopped - marking sequence as completed (from stop timer)");
        // 重置标志
        if (state_mutex_) {
            xSemaphoreTake(state_mutex_, portMAX_DELAY);
            is_last_sequence_move_ = false;
            xSemaphoreGive(state_mutex_);
        } else {
            is_last_sequence_move_ = false;
        }
        
        // 延迟50ms后调用MarkSequenceCompleted，确保GPIO状态完全稳定
        // 使用xTimerPendFunctionCall，避免阻塞当前上下文（可能是定时器回调）
        BaseType_t result = xTimerPendFunctionCall(
            [](void* self_ptr, uint32_t param) {
                EarController* ear_controller = static_cast<EarController*>(self_ptr);
                if (ear_controller) {
                    ear_controller->MarkSequenceCompleted();
                }
            },
            this, 0, pdMS_TO_TICKS(50)
        );
        
        if (result != pdPASS) {
            ESP_LOGW(TAG, "[SEQUENCE] Failed to schedule MarkSequenceCompleted, executing directly");
            MarkSequenceCompleted();
        }
    } else if (is_last_sequence_move && !is_sequence_still_active) {
        // 序列已经完成（可能被其他方式停止），只重置标志
        ESP_LOGI(TAG, "[SEQUENCE] Last sequence step stopped but sequence already completed, resetting flag");
        if (state_mutex_) {
            xSemaphoreTake(state_mutex_, portMAX_DELAY);
            is_last_sequence_move_ = false;
            xSemaphoreGive(state_mutex_);
        } else {
            is_last_sequence_move_ = false;
        }
    }
    
    return ESP_OK;
}

// ===== 双耳组合控制接口实现 =====
esp_err_t Tc118sEarController::MoveBoth(ear_combo_param_t combo) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    uint32_t duration_ms = combo.duration_ms;
    if (duration_ms < EAR_BOTH_MIN_DURATION_MS && duration_ms > 0) {
        duration_ms = EAR_BOTH_MIN_DURATION_MS;
    }

    uint64_t now_ms = esp_timer_get_time() / 1000;
    
    // 保护状态变量访问
    bool is_moving = false;
    ear_combo_action_t previous_action = EAR_COMBO_BOTH_STOP;
    uint64_t previous_start_time_ms = 0;
    if (state_mutex_) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        is_moving = moving_both_;
        previous_action = current_combo_action_;
        previous_start_time_ms = last_combo_start_time_ms_;
        xSemaphoreGive(state_mutex_);
    } else {
        is_moving = moving_both_;
        previous_action = current_combo_action_;
        previous_start_time_ms = last_combo_start_time_ms_;
    }
    
    if (combo.combo_action == EAR_COMBO_BOTH_STOP) {
        ESP_LOGI(TAG, "MoveBoth received STOP action");
        StopBoth();
        return ESP_OK;
    }

    if ((now_ms - last_move_tick_ms_) < EAR_MOVE_COOLDOWN_MS &&
        combo.combo_action == previous_action) {
        ESP_LOGD(TAG, "MoveBoth cooldown: combo=%d, extending duration=%lu ms",
                 combo.combo_action, duration_ms);
        ScheduleComboStop(duration_ms);
        return ESP_OK;
    }

    bool same_action = is_moving && (previous_action == combo.combo_action);
    if (same_action) {
        ESP_LOGD(TAG, "MoveBoth re-entry with same action=%d, duration=%lu ms",
                 combo.combo_action, duration_ms);
    } else if (is_moving) {
        // 记录动作切换时的持续时间信息
        uint64_t switch_time_ms = esp_timer_get_time() / 1000;
        if (previous_start_time_ms > 0) {
            EAR_LOG_DURATION(TAG, "[DURATION] Action change: %d -> %d, previous action elapsed=%" PRIu64 " ms", 
                     previous_action, combo.combo_action, switch_time_ms - previous_start_time_ms);
            
            // 如果上一个动作还没执行完就被切换，记录警告
            if (gpio_set_time_ms_ > 0 && scheduled_duration_ms_ > 0) {
                uint64_t actual_elapsed_ms = switch_time_ms - gpio_set_time_ms_;
                if (actual_elapsed_ms < scheduled_duration_ms_) {
                    ESP_LOGW(TAG, "[DURATION] Action interrupted: elapsed=%" PRIu64 " ms < scheduled=%lu ms (short by %" PRIu64 " ms)", 
                             actual_elapsed_ms, scheduled_duration_ms_, scheduled_duration_ms_ - actual_elapsed_ms);
                }
            }
        }
        
        ESP_LOGI(TAG, "MoveBoth action change: %d -> %d", previous_action, combo.combo_action);
        // 动作变化时，先停止当前动作的GPIO，避免快速反转
        // 注意：这里只停止GPIO，不调用StopBoth()（因为StopBoth会重置状态）
        SetGpioLevels(true, EAR_ACTION_STOP);
        SetGpioLevels(false, EAR_ACTION_STOP);
        // 取消之前的停止定时器
        auto& app = Application::GetInstance();
        app.CancelEarComboStopTimer();
        if (stop_timer_) {
            xTimerStop(stop_timer_, 0);
        }
        
        // 重置持续时间监控（因为动作被提前停止）
        gpio_set_time_ms_ = 0;
        scheduled_duration_ms_ = 0;
        stop_timer_scheduled_time_ms_ = 0;
    }

    last_move_tick_ms_ = now_ms;

    EAR_LOG_VERBOSE(TAG, "Moving both ears: combo=%d, duration=%lu ms", 
             combo.combo_action, duration_ms);
    
    // 记录计划持续时间（用于后续验证）
    scheduled_duration_ms_ = duration_ms;
    
    // 根据组合动作类型设置双耳GPIO状态（移除阻塞性错峰启动）
    switch (combo.combo_action) {
        case EAR_COMBO_BOTH_FORWARD:
        case EAR_COMBO_BOTH_BACKWARD:
            // 非阻塞启动：双耳同时启动，不阻塞Worker Task
            StartBothWithStagger(combo.combo_action, combo.duration_ms);
            break;
            
        case EAR_COMBO_BOTH_STOP:
            SetGpioLevels(true, EAR_ACTION_STOP);
            SetGpioLevels(false, EAR_ACTION_STOP);
            break;
            
        case EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD:
            // 记录 GPIO 设置时间
            gpio_set_time_ms_ = esp_timer_get_time() / 1000;
            SetGpioLevels(true, EAR_ACTION_FORWARD);
            // 右耳保持当前状态，不改变
            EAR_LOG_DURATION(TAG, "[DURATION] GPIO set at: %" PRIu64 " ms, scheduled duration: %lu ms", 
                     gpio_set_time_ms_, scheduled_duration_ms_);
            break;
            
        case EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD:
            // 左耳保持当前状态，不改变
            // 记录 GPIO 设置时间
            gpio_set_time_ms_ = esp_timer_get_time() / 1000;
            SetGpioLevels(false, EAR_ACTION_FORWARD);
            EAR_LOG_DURATION(TAG, "[DURATION] GPIO set at: %" PRIu64 " ms, scheduled duration: %lu ms", 
                     gpio_set_time_ms_, scheduled_duration_ms_);
            break;
            
        case EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD:
            // 记录 GPIO 设置时间
            gpio_set_time_ms_ = esp_timer_get_time() / 1000;
            SetGpioLevels(true, EAR_ACTION_FORWARD);
            SetGpioLevels(false, EAR_ACTION_BACKWARD);
            EAR_LOG_DURATION(TAG, "[DURATION] GPIO set at: %" PRIu64 " ms, scheduled duration: %lu ms", 
                     gpio_set_time_ms_, scheduled_duration_ms_);
            break;
            
        case EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD:
            // 记录 GPIO 设置时间
            gpio_set_time_ms_ = esp_timer_get_time() / 1000;
            SetGpioLevels(true, EAR_ACTION_BACKWARD);
            SetGpioLevels(false, EAR_ACTION_FORWARD);
            EAR_LOG_DURATION(TAG, "[DURATION] GPIO set at: %" PRIu64 " ms, scheduled duration: %lu ms", 
                     gpio_set_time_ms_, scheduled_duration_ms_);
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown combo action: %d", combo.combo_action);
            return ESP_ERR_INVALID_ARG;
    }
    
    uint64_t action_start_time_ms = same_action && previous_start_time_ms != 0
        ? previous_start_time_ms
        : now_ms;
    UpdateComboState(true, combo.combo_action, action_start_time_ms);

    ScheduleComboStop(duration_ms);
    
    // P0修复：如果设置了is_last_sequence_move_标志，在MoveBoth后保持标志
    // 这样stop timer回调可以检查这个标志
    
    return ESP_OK;
}

// ===== 位置控制接口实现 =====
esp_err_t Tc118sEarController::SetEarPosition(bool left_ear, ear_position_t position) {
    if (!initialized_) {
        return ESP_ERR_INVALID_STATE;
    }
    
    ear_action_param_t action;
    
    switch (position) {
        case EAR_POSITION_DOWN:
            // 修改：FORWARD 现在朝向 DOWN（下垂）
            action = {EAR_ACTION_FORWARD, EAR_POSITION_DOWN_TIME_MS};
            break;
        case EAR_POSITION_UP:
            // 修改：BACKWARD 现在朝向 UP（竖起）
            action = {EAR_ACTION_BACKWARD, EAR_POSITION_UP_TIME_MS};
            break;
        case EAR_POSITION_MIDDLE: {
            // 根据当前位置计算需要的动作
            ear_position_t current = GetEarPosition(left_ear);
            if (current == EAR_POSITION_UP) {
                // 从 UP 到 MIDDLE：使用 FORWARD（朝 DOWN 方向）
                action = {EAR_ACTION_FORWARD, EAR_POSITION_MIDDLE_TIME_MS};
            } else {
                // 从 DOWN 到 MIDDLE：使用 BACKWARD（朝 UP 方向）
                action = {EAR_ACTION_BACKWARD, EAR_POSITION_MIDDLE_TIME_MS};
            }
            break;
        }
        default:
            ESP_LOGW(TAG, "Unknown ear position: %d", position);
            return ESP_ERR_INVALID_ARG;
    }
    
    // 执行动作
    esp_err_t ret = MoveEar(left_ear, action);
    
    // 更新位置状态
    if (ret == ESP_OK) {
        if (left_ear) {
            left_ear_position_ = position;
        } else {
            right_ear_position_ = position;
        }
    }
    
    return ret;
}

ear_position_t Tc118sEarController::GetEarPosition(bool left_ear) {
    return left_ear ? left_ear_position_ : right_ear_position_;
}

esp_err_t Tc118sEarController::ResetToDefault() {
    ESP_LOGI(TAG, "Resetting ears to default position (DOWN)");
    
    esp_err_t ret1 = SetEarPosition(true, EAR_POSITION_DOWN);
    esp_err_t ret2 = SetEarPosition(false, EAR_POSITION_DOWN);
    
    if (ret1 == ESP_OK && ret2 == ESP_OK) {
        ESP_LOGI(TAG, "Ears reset to default position successfully");
        return ESP_OK;
    } else {
        ESP_LOGW(TAG, "Failed to reset ears to default position");
        return ESP_FAIL;
    }
}

// ===== 序列控制接口实现 =====
esp_err_t Tc118sEarController::PlaySequence(const ear_sequence_step_t* steps, uint8_t count, bool loop) {
    if (!steps || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    StopSequence();
    
    // 设置序列
    current_sequence_.clear();
    for (uint8_t i = 0; i < count; i++) {
        current_sequence_.push_back(steps[i]);
        // 添加调试日志：验证序列数据是否正确
        EAR_LOG_DEBUG(TAG, "[SEQUENCE] Load step %d: action=%d, duration=%lu ms, delay=%lu ms", 
                 i + 1, static_cast<int>(steps[i].combo_action), steps[i].duration_ms, steps[i].delay_ms);
    }
    
    // 开始序列
    current_step_index_ = 0;
    current_loop_count_ = 0;
    sequence_active_ = true;
    
    // 启动定时器
    xTimerStart(sequence_timer_, 0);
    
    EAR_LOG_VERBOSE(TAG, "Started sequence with %d steps, loop: %s", count, loop ? "true" : "false");
    return ESP_OK;
}

esp_err_t Tc118sEarController::StopSequence() {
    EAR_LOG_VERBOSE(TAG, "StopSequence called: sequence_active=%s, emotion_action_active=%s", 
             sequence_active_ ? "true" : "false", emotion_action_active_ ? "true" : "false");
    
    // 无论是否有活跃序列，都应该重置 emotion_action_active_ 状态
    // 这样即使序列已经完成但状态未清除，也能正确处理后续情绪触发
    if (sequence_active_) {
        sequence_active_ = false;
        
        if (sequence_timer_) {
            xTimerStop(sequence_timer_, 0);
        }
        
        StopBoth();
        ESP_LOGI(TAG, "Sequence stopped and state reset");
    } else {
        EAR_LOG_VERBOSE(TAG, "No active sequence to stop");
    }
    
    // 无条件重置情绪激活状态，确保下次可以触发
    emotion_action_active_ = false;
    
    return ESP_OK;
}

// ===== 情绪控制接口实现 =====

esp_err_t Tc118sEarController::SetEmotion(const char* emotion, const ear_sequence_step_t* steps, uint8_t count) {
    if (!emotion || !steps || count == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 创建序列
    std::vector<ear_sequence_step_t> sequence;
    for (uint8_t i = 0; i < count; i++) {
        sequence.push_back(steps[i]);
    }
    
    // 存储到映射表
    std::string emotion_str(emotion);
    emotion_mappings_[emotion_str] = sequence;
    
    ESP_LOGI(TAG, "Custom emotion mapping set: %s -> %d steps", emotion, count);
    return ESP_OK;
}

esp_err_t Tc118sEarController::TriggerEmotion(const char* emotion) {
    if (!emotion) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // 检查是否应该触发情绪
    if (!ShouldTriggerEmotion(emotion)) {
        EAR_LOG_VERBOSE(TAG, "Emotion trigger skipped for: %s", emotion);
        return ESP_OK;
    }
    
    std::string emotion_str(emotion);
    auto it = emotion_mappings_.find(emotion_str);
    
    if (it == emotion_mappings_.end()) {
        ESP_LOGW(TAG, "Unknown emotion: %s", emotion);
        return ESP_ERR_NOT_FOUND;
    }
    
    const std::vector<ear_sequence_step_t>& sequence = it->second;
    
    // 验证序列数据（调试）
    ESP_LOGI(TAG, "[EMOTION] Triggering emotion '%s' with %d steps", emotion, static_cast<int>(sequence.size()));
    for (size_t i = 0; i < sequence.size(); ++i) {
        // 直接使用sequence[i]访问，避免未使用变量警告
        EAR_LOG_VERBOSE(TAG, "[EMOTION]   Step %d: action=%d, duration=%lu ms, delay=%lu ms", 
                 static_cast<int>(i + 1), static_cast<int>(sequence[i].combo_action), sequence[i].duration_ms, sequence[i].delay_ms);
        
        // 额外验证：打印结构体地址和内存内容（调试用）
        EAR_LOG_DEBUG(TAG, "[EMOTION]     Step ptr=%p, sizeof(step)=%zu, action=%d, duration=%lu, delay=%lu", 
                 static_cast<const void*>(&sequence[i]), sizeof(sequence[i]), 
                 static_cast<int>(sequence[i].combo_action), sequence[i].duration_ms, sequence[i].delay_ms);
    }
    
    // 更新情绪状态
    UpdateEmotionState(emotion);
    
    // 播放序列
    if (!sequence.empty()) {
        return PlaySequence(sequence.data(), sequence.size(), false);
    }
    
    return ESP_OK;
}

esp_err_t Tc118sEarController::StopEmotion() {
    ESP_LOGI(TAG, "Stopping emotion action");
    return StopSequence();
}

// ===== 状态查询接口实现 =====

ear_action_t Tc118sEarController::GetCurrentAction(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->current_action;
}

bool Tc118sEarController::IsMoving(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear_ : &right_ear_;
    return ear->is_active;
}

bool Tc118sEarController::IsSequenceActive() {
    return sequence_active_;
}

// ===== 私有方法实现 =====

void Tc118sEarController::InitializeDefaultEmotionMappings() {
    // 复制默认映射到实例映射表
    for (const auto& pair : default_emotion_mappings_) {
        emotion_mappings_[pair.first] = pair.second;
        
        // 验证序列数据是否正确（特别检查 sad 情绪）
        if (pair.first == "sad" && !pair.second.empty()) {
            // 调试日志：只在详细日志模式下显示
            EAR_LOG_DEBUG(TAG, "[EMOTION] Loading 'sad' emotion with %d steps", static_cast<int>(pair.second.size()));
            
            // 直接检查原始数组数据
            EAR_LOG_DEBUG(TAG, "[EMOTION] Original sad_sequence_[0]: action=%d, duration=%lu, delay=%lu", 
                     static_cast<int>(sad_sequence_[0].combo_action), 
                     sad_sequence_[0].duration_ms, sad_sequence_[0].delay_ms);
            
            // 检查复制后的 vector 数据
            EAR_LOG_DEBUG(TAG, "[EMOTION] Copied vector[0]: action=%d, duration=%lu, delay=%lu", 
                     static_cast<int>(pair.second[0].combo_action), 
                     pair.second[0].duration_ms, pair.second[0].delay_ms);
            
            // 验证数据是否一致
            if (sad_sequence_[0].duration_ms != pair.second[0].duration_ms ||
                sad_sequence_[0].delay_ms != pair.second[0].delay_ms) {
                ESP_LOGW(TAG, "[EMOTION] Data mismatch detected in 'sad' emotion!");
            }
        }
        
        EAR_LOG_DEBUG(TAG, "[EMOTION] Loaded emotion '%s' with %d steps", 
                 pair.first.c_str(), static_cast<int>(pair.second.size()));
        for (size_t i = 0; i < pair.second.size(); ++i) {
            EAR_LOG_DEBUG(TAG, "[EMOTION]   Step %d: action=%d, duration=%lu ms, delay=%lu ms", 
                     static_cast<int>(i + 1), static_cast<int>(pair.second[i].combo_action), 
                     pair.second[i].duration_ms, pair.second[i].delay_ms);
        }
    }
    ESP_LOGI(TAG, "Default emotion mappings initialized");
}

void Tc118sEarController::SetupSequencePatterns() {
    ESP_LOGI(TAG, "Sequence patterns setup completed");
}

void Tc118sEarController::OnSequenceTimer(TimerHandle_t timer) {
    if (!sequence_active_ || current_sequence_.empty()) {
        return;
    }
    
    // 执行当前步骤
    ear_sequence_step_t step = current_sequence_[current_step_index_];
    
    // 记录序列步骤开始时间（用于验证序列执行时间）
    uint64_t step_start_time_ms = esp_timer_get_time() / 1000;
    // 使用 %llu 格式，因为 ESP-IDF 可能不完全支持 PRIu64 宏展开
    ESP_LOGI(TAG, "[SEQUENCE] Step %d/%d: action=%d, duration=%lu ms, delay=%lu ms, at=%llu ms", 
             current_step_index_ + 1, static_cast<int>(current_sequence_.size()), 
             static_cast<int>(step.combo_action), step.duration_ms, step.delay_ms, 
             (unsigned long long)step_start_time_ms);
    
    // 检查上一个动作是否还在执行（如果存在）
    if (gpio_set_time_ms_ > 0 && scheduled_duration_ms_ > 0) {
        uint64_t elapsed_ms = step_start_time_ms - gpio_set_time_ms_;
        if (elapsed_ms < scheduled_duration_ms_) {
            EAR_LOG_VERBOSE(TAG, "[SEQUENCE] Previous action still running: elapsed=%" PRIu64 " ms, scheduled=%lu ms (interrupted)", 
                     elapsed_ms, scheduled_duration_ms_);
        }
    }
    
    // 执行组合动作 - 改为主循环/Worker上下文执行，避免定时器回调阻塞
    ear_combo_param_t combo = {step.combo_action, step.duration_ms};
    // 投递到外设 Worker 执行 GPIO，避免占用主循环
    auto& app = Application::GetInstance();
    
    // 获取队列句柄用于监控
    QueueHandle_t queue = app.GetPeripheralTaskQueue();
    
    // P0修复：检查是否是序列的最后一个步骤
    // 条件：当前步骤是序列中的最后一步（current_step_index_ + 1 >= size），且只循环一次（current_loop_count_ == 0）
    bool is_last_step = (current_step_index_ + 1 >= current_sequence_.size()) && (current_loop_count_ == 0);
    
    auto task = std::make_unique<Application::PeripheralTask>();
    task->action = Application::PeripheralAction::kEarSequence;
    task->combo_action = static_cast<int>(combo.combo_action);
    task->duration_ms = combo.duration_ms;
    task->source = Application::PeripheralTaskSource::kSequence;
    task->is_last_sequence_step = is_last_step;  // P0修复：标记最后一个任务
    
    bool enqueued = app.EnqueuePeripheralTask(std::move(task));
    
    // 监控队列状态（投递后）
    if (queue) {
        UBaseType_t queue_waiting_after = uxQueueMessagesWaiting(queue);
        UBaseType_t queue_spaces = uxQueueSpacesAvailable(queue);
        UBaseType_t queue_total = queue_waiting_after + queue_spaces;
        
        if (queue_waiting_after > queue_total / 2) {
            ESP_LOGW(TAG, "[QUEUE] Queue usage high: %u/%u (%.1f%%), step %d/%d",
                     queue_waiting_after, queue_total,
                     100.0f * queue_waiting_after / queue_total,
                     current_step_index_ + 1, static_cast<int>(current_sequence_.size()));
        } else {
            EAR_LOG_VERBOSE(TAG, "[QUEUE] Step %d/%d: queue=%u/%u waiting%s",
                     current_step_index_ + 1, static_cast<int>(current_sequence_.size()),
                     queue_waiting_after, queue_total,
                     is_last_step ? " [LAST STEP]" : "");
        }
    }
    
    if (!enqueued) {
        ESP_LOGW(TAG, "Failed to enqueue ear sequence task, combo=%d", static_cast<int>(combo.combo_action));
    }
    
    // 移动到下一步
    current_step_index_++;
    
            // 检查序列是否完成
            bool sequence_just_completed = false;
            if (current_step_index_ >= current_sequence_.size()) {
                current_step_index_ = 0;
                current_loop_count_++;
                
                // 检查循环是否完成
                if (current_loop_count_ >= 1) {
                    // P0修复：序列完成，但不立即设置完成标志
                    // 完成标志将在Worker处理完最后一个任务后设置（通过is_last_sequence_step标记）
                    sequence_just_completed = true;  // 标记序列刚刚完成
                    ESP_LOGI(TAG, "[SEQUENCE] Last step enqueued - completion will be handled by Worker");
                    
                    // 序列完成，检查队列状态（仅用于监控）
                    QueueHandle_t queue = app.GetPeripheralTaskQueue();
                    if (queue) {
                        UBaseType_t queue_waiting = uxQueueMessagesWaiting(queue);
                        UBaseType_t queue_spaces = uxQueueSpacesAvailable(queue);
                        UBaseType_t queue_total = queue_waiting + queue_spaces;
                        
                        ESP_LOGI(TAG, "[SEQUENCE] Sequence completing - Queue status: %u/%u waiting (%.1f%%), %u spaces",
                                 queue_waiting, queue_total,
                                 queue_total > 0 ? 100.0f * queue_waiting / queue_total : 0.0f,
                                 queue_spaces);
                        
                        if (queue_waiting > 0) {
                            ESP_LOGW(TAG, "[SEQUENCE] WARNING: %u tasks still in queue when sequence completes! "
                                     "These tasks may cause action interruption.", queue_waiting);
                            
                            // 记录队列积压的详细信息
                            if (queue_waiting > queue_total / 2) {
                                ESP_LOGE(TAG, "[SEQUENCE] CRITICAL: Queue usage exceeds 50%%! "
                                         "Worker may be overloaded or blocked.");
                            }
                        } else {
                            ESP_LOGI(TAG, "[SEQUENCE] Queue is empty - good timing");
                        }
                    }
                    
                    // P0修复：停止定时器，但不设置完成标志（由Worker处理）
                    if (sequence_timer_) {
                        xTimerStop(sequence_timer_, 0);
                    }
                    
                    // 关键修复：设置sequence_active_ = false，防止后续代码重新启动定时器
                    // 但保持emotion_action_active_ = true，等待Worker完成最后一个任务
                    sequence_active_ = false;
                    ESP_LOGI(TAG, "[SEQUENCE] Sequence timer stopped, waiting for Worker to complete last step");
                    
                    // P0修复：不调用ScheduleEarFinalPosition()，等待Worker处理完最后一个任务后调用
                    return;  // 直接返回，防止后续代码重新设置定时器
                }
            }
            
            // 设置下一步的定时器（只有在序列未完成时才执行）
            if (sequence_active_ && !sequence_just_completed) {
                uint32_t next_delay = step.delay_ms;
                if (next_delay == 0) {
                    next_delay = SCENARIO_DEFAULT_DELAY_MS;
                }
                // 确保定时器周期 >= 动作持续时间 + 暂停时间，避免动作重叠
                uint32_t total_time = step.duration_ms + next_delay;
                if (total_time < SCENARIO_DEFAULT_DELAY_MS) {
                    total_time = SCENARIO_DEFAULT_DELAY_MS;
                }
                xTimerChangePeriod(sequence_timer_, MS_TO_TICKS_MIN1(total_time), 0);
            }
}

bool Tc118sEarController::ShouldTriggerEmotion(const char* emotion) {
    if (!emotion) {
        ESP_LOGW(TAG, "ShouldTriggerEmotion: emotion is null");
        return false;
    }
    
    // 获取当前时间
    uint64_t current_time = esp_timer_get_time() / 1000;
    
    // 使用互斥锁保护状态检查，避免竞态条件
    bool is_sequence_active = false;
    bool is_emotion_active = false;
    if (state_mutex_) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        is_sequence_active = sequence_active_;
        is_emotion_active = emotion_action_active_;
        xSemaphoreGive(state_mutex_);
    } else {
        is_sequence_active = sequence_active_;
        is_emotion_active = emotion_action_active_;
    }
    
    EAR_LOG_VERBOSE(TAG, "ShouldTriggerEmotion: checking %s, current_emotion=%s, emotion_action_active=%s, sequence_active=%s", 
             emotion, current_emotion_.c_str(), is_emotion_active ? "true" : "false", is_sequence_active ? "true" : "false");
    
    // 如果当前有序列正在进行，不触发新的情绪
    if (is_sequence_active) {
        ESP_LOGI(TAG, "Sequence already active, skipping trigger for %s", emotion);
        return false;
    }
    
    // 如果当前有情绪动作正在进行，不触发新的情绪
    if (is_emotion_active) {
        ESP_LOGI(TAG, "Emotion action already active, skipping trigger for %s", emotion);
        return false;
    }
    
    // 如果情绪相同且还在冷却期内，不触发
    if (current_emotion_ == emotion && 
        (current_time - last_emotion_time_) < EMOTION_COOLDOWN_MS) {
        ESP_LOGI(TAG, "Emotion %s still in cooldown (%" PRIu64 " ms remaining), skipping trigger", 
                 emotion, EMOTION_COOLDOWN_MS - (current_time - last_emotion_time_));
        return false;
    }
    
    EAR_LOG_VERBOSE(TAG, "ShouldTriggerEmotion: %s is allowed to trigger", emotion);
    return true;
}

void Tc118sEarController::UpdateEmotionState(const char* emotion) {
    if (!emotion) {
        return;
    }
    
    current_emotion_ = emotion;
    last_emotion_time_ = esp_timer_get_time() / 1000;
    emotion_action_active_ = true;
    
    EAR_LOG_VERBOSE(TAG, "Updated emotion state: %s, time: %llu", emotion, (unsigned long long)last_emotion_time_);
}

void Tc118sEarController::SetEarFinalPosition() {
    // 设置耳朵到中立位置（居中），保持激活状态
    ESP_LOGI(TAG, "Setting ears to neutral MIDDLE position");
    SetEarPosition(true, EAR_POSITION_MIDDLE);
    SetEarPosition(false, EAR_POSITION_MIDDLE);
}

void Tc118sEarController::ScheduleEarFinalPosition() {
    // 使用延迟队列委托位置设置，避免阻塞定时器回调
    // 延迟50ms执行，确保序列完全完成
    BaseType_t result = xTimerPendFunctionCall(
        [](void* self_ptr, uint32_t param) {
            Tc118sEarController* self = static_cast<Tc118sEarController*>(self_ptr);
            self->SetEarFinalPosition();
        },
        this,
        0,
        pdMS_TO_TICKS(50)
    );
    
    if (result != pdPASS) {
        ESP_LOGW(TAG, "Failed to schedule ear final position, executing directly");
        // 如果调度失败，直接执行（虽然会阻塞，但总比不执行好）
        SetEarFinalPosition();
    }
}

// P0修复：标记当前MoveBoth是否是序列的最后一个步骤
void Tc118sEarController::SetLastSequenceMoveFlag(bool is_last) {
    if (state_mutex_) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        is_last_sequence_move_ = is_last;
        xSemaphoreGive(state_mutex_);
    } else {
        is_last_sequence_move_ = is_last;
    }
    
    ESP_LOGI(TAG, "[SEQUENCE] SetLastSequenceMoveFlag: %s", is_last ? "true" : "false");
}

// P0修复：重写基类方法，标记序列完成
// 现在从StopBoth中调用（当stop timer触发时），确保在GPIO真正停止后才调用
void Tc118sEarController::MarkSequenceCompleted() {
    ESP_LOGI(TAG, "[SEQUENCE] Marking sequence as completed (from stop timer)");
    
    // 设置完成标志
    sequence_active_ = false;
    emotion_action_active_ = false;
    
    EAR_LOG_VERBOSE(TAG, "Sequence completed, resetting emotion state");
    
    // 停止定时器（如果还在运行）
    if (sequence_timer_) {
        xTimerStop(sequence_timer_, 0);
    }
    
    // P0修复：StopBoth已经在stop timer回调中调用过了，GPIO已经停止
    // 直接调用ScheduleEarFinalPosition，无需再次StopBoth
    // 延迟50ms确保GPIO状态稳定
    ScheduleEarFinalPosition();
}

void Tc118sEarController::SetEarInitialPosition() {
    // 系统初始化时设置耳朵到下垂位置
    ESP_LOGI(TAG, "Setting ears to initial DOWN position for system startup");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
}


// 强制重置所有状态 - 用于调试和紧急情况
void Tc118sEarController::ForceResetAllStates() {
    ESP_LOGI(TAG, "ForceResetAllStates: Resetting all ear controller states");
    
    // 停止序列
    StopSequence();
    
    // 重置状态
    current_emotion_ = "neutral";
    last_emotion_time_ = 0;
    
    // 停止所有耳朵
    StopBoth();
    
    // 设置到初始化位置（下垂）
    SetEarInitialPosition();
    
    ESP_LOGI(TAG, "ForceResetAllStates: All states reset successfully");
}

// ===== 新增：基础功能测试方法实现 =====

void Tc118sEarController::TestBasicEarFunctions() {
    ESP_LOGI(TAG, "=== Testing Basic Ear Functions ===");
    
    // 测试单耳控制
    ESP_LOGI(TAG, "Testing LEFT ear FORWARD");
    MoveEar(true, {EAR_ACTION_FORWARD, EAR_POSITION_UP_TIME_MS});
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Testing LEFT ear BACKWARD");
    MoveEar(true, {EAR_ACTION_BACKWARD, EAR_POSITION_DOWN_TIME_MS});
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Testing RIGHT ear FORWARD");
    MoveEar(false, {EAR_ACTION_FORWARD, EAR_POSITION_UP_TIME_MS});
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Testing RIGHT ear BACKWARD");
    MoveEar(false, {EAR_ACTION_BACKWARD, EAR_POSITION_DOWN_TIME_MS});
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "Stopping both ears");
    StopBoth();
    ESP_LOGI(TAG, "=== Basic Functions Test Completed ===");
}

void Tc118sEarController::TestEarPositions() {
    ESP_LOGI(TAG, "=== Testing Ear Positions ===");
    
    // 测试位置控制
    ESP_LOGI(TAG, "Setting both ears to UP position");
    SetEarPosition(true, EAR_POSITION_UP);
    SetEarPosition(false, EAR_POSITION_UP);
    vTaskDelay(pdMS_TO_TICKS(800));
    
    ESP_LOGI(TAG, "Setting both ears to DOWN position");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "Setting both ears to UP position");
    SetEarPosition(true, EAR_POSITION_UP);
    SetEarPosition(false, EAR_POSITION_UP);
    vTaskDelay(pdMS_TO_TICKS(800));
    
    ESP_LOGI(TAG, "Setting both ears to DOWN position");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
    vTaskDelay(pdMS_TO_TICKS(800));
    /////////////////////////////////////////
    ESP_LOGI(TAG, "Setting both ears to UP position");
    SetEarPosition(true, EAR_POSITION_UP);
    SetEarPosition(false, EAR_POSITION_UP);
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "Setting both ears to MIDDLE position");
    SetEarPosition(true, EAR_POSITION_MIDDLE);
    SetEarPosition(false, EAR_POSITION_MIDDLE);
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "Setting both ears to UP position");
    SetEarPosition(true, EAR_POSITION_UP);
    SetEarPosition(false, EAR_POSITION_UP);
    vTaskDelay(pdMS_TO_TICKS(800));
    
    ESP_LOGI(TAG, "Setting both ears to MIDDLE position");
    SetEarPosition(true, EAR_POSITION_MIDDLE);
    SetEarPosition(false, EAR_POSITION_MIDDLE);
    vTaskDelay(pdMS_TO_TICKS(800));
    /////////////////////////////////////////
    ESP_LOGI(TAG, "Setting both ears to DOWN position");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "Setting both ears to MIDDLE position");
    SetEarPosition(true, EAR_POSITION_MIDDLE);
    SetEarPosition(false, EAR_POSITION_MIDDLE);
    vTaskDelay(pdMS_TO_TICKS(800));
    
    ESP_LOGI(TAG, "Setting both ears to DOWN position");
    SetEarPosition(true, EAR_POSITION_DOWN);
    SetEarPosition(false, EAR_POSITION_DOWN);
    vTaskDelay(pdMS_TO_TICKS(800));

    ESP_LOGI(TAG, "Setting both ears to MIDDLE position");
    SetEarPosition(true, EAR_POSITION_MIDDLE);
    SetEarPosition(false, EAR_POSITION_MIDDLE);
    vTaskDelay(pdMS_TO_TICKS(800));

    
    ESP_LOGI(TAG, "=== Position Test Completed ===");
}

void Tc118sEarController::TestEarCombinations() {
    ESP_LOGI(TAG, "=== Testing Ear Combinations ===");
    ESP_LOGI(TAG, "Each combination will be tested twice for clear observation");
    
    ear_combo_param_t combo;
    
    // 1. 双耳同时向前 (EAR_COMBO_BOTH_FORWARD)
    ESP_LOGI(TAG, "--- Test 1: Both ears forward ---");
    combo = {EAR_COMBO_BOTH_FORWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    combo = {EAR_COMBO_BOTH_FORWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 2. 双耳同时向后 (EAR_COMBO_BOTH_BACKWARD)
    ESP_LOGI(TAG, "--- Test 2: Both ears backward ---");
    combo = {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    combo = {EAR_COMBO_BOTH_BACKWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 3. 左耳向前，右耳保持 (EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD)
    ESP_LOGI(TAG, "--- Test 3: Left forward, right hold ---");
    combo = {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    combo = {EAR_COMBO_LEFT_FORWARD_RIGHT_HOLD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 4. 左耳保持，右耳向前 (EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD)
    ESP_LOGI(TAG, "--- Test 4: Left hold, right forward ---");
    combo = {EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    combo = {EAR_COMBO_LEFT_HOLD_RIGHT_FORWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 5. 左耳向前，右耳向后 (EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD)
    ESP_LOGI(TAG, "--- Test 5: Left forward, right backward ---");
    combo = {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    combo = {EAR_COMBO_LEFT_FORWARD_RIGHT_BACKWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 6. 左耳向后，右耳向前 (EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD)
    ESP_LOGI(TAG, "--- Test 6: Left backward, right forward ---");
    combo = {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    combo = {EAR_COMBO_LEFT_BACKWARD_RIGHT_FORWARD, EAR_MOVE_FULL_MS};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(1000));
    
    // 7. 双耳停止 (EAR_COMBO_BOTH_STOP) - 最后测试停止功能
    ESP_LOGI(TAG, "--- Test 6: Both ears stop ---");
    combo = {EAR_COMBO_BOTH_STOP, 0};
    MoveBoth(combo);
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "=== Combination Test Completed ===");
}

void Tc118sEarController::TestEarSequences() {
    ESP_LOGI(TAG, "=== Testing Emotion-Triggered Ear Sequences ===");
    ESP_LOGI(TAG, "Each emotion will be triggered twice for clear observation");
    
    // 首先强制重置所有状态，确保测试环境干净
    ESP_LOGI(TAG, "Force resetting all states before testing...");
    ForceResetAllStates();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // 测试各种情绪触发 - 包含所有主要情绪序列
    const char* test_emotions[] = {
        "happy",      // 开心：摇摆2-3次，幅度大，最后停留在竖起位置
        "excited",    // 超级兴奋：短时间前后摇摆5-6次，幅度小强度大
        "curious",    // 好奇：使用交叉动作，左右耳相反方向运动
        "playful",    // 顽皮：快速变化，加入交叉动作
        "surprised",  // 惊讶：快速竖起，然后缓慢下垂
        "sad",        // 悲伤：摇摆2-3次，前向幅度大，后向幅度小，最后停留在下摆位置
        "sleepy",     // 困倦：缓慢下垂，最后停留在下摆位置
        "confident",  // 自信：稳定节奏，最后停留在竖起位置
        "confused",   // 困惑：使用正反交叉动作交替，表达混乱
        "loving",     // 爱意：温柔节奏，幅度中等
        "angry",      // 愤怒：快速节奏，小幅快速摆动
        "cool"        // 酷：慢速节奏，幅度大，最后停留在竖起位置
    };
    
    const int emotion_count = sizeof(test_emotions) / sizeof(test_emotions[0]);
    
    for (int i = 0; i < emotion_count; i++) {
        const char* emotion = test_emotions[i];
        ESP_LOGI(TAG, "\n--- Testing emotion: %s (will trigger twice) ---", emotion);
        
        // 第一次触发
        ESP_LOGI(TAG, "  [1/2] Triggering emotion: %s", emotion);
        esp_err_t ret = TriggerEmotion(emotion);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  [1/2] Emotion '%s' triggered successfully", emotion);
            
            // 等待情绪序列完成
            while (IsSequenceActive()) {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            
            // 两次触发之间的间隔
            vTaskDelay(pdMS_TO_TICKS(3000));
            
            // 第二次触发
            ESP_LOGI(TAG, "  [2/2] Triggering emotion: %s again", emotion);
            ret = TriggerEmotion(emotion);
            if (ret == ESP_OK) {
                ESP_LOGI(TAG, "  [2/2] Emotion '%s' triggered successfully", emotion);
                
                // 等待情绪序列完成
                while (IsSequenceActive()) {
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
            } else {
                ESP_LOGE(TAG, "  [2/2] Failed to trigger emotion '%s': %s", emotion, esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "  [1/2] Failed to trigger emotion '%s': %s", emotion, esp_err_to_name(ret));
        }
        
        // 情绪之间的间隔（让测试更清晰）
        ESP_LOGI(TAG, "  Emotion '%s' test completed, waiting before next emotion...", emotion);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
    
    // 重置到默认位置
    ESP_LOGI(TAG, "\nResetting ears to default position...");
    ForceResetAllStates();
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "=== Emotion-Triggered Sequence Test Completed ===");
}

// 停止定时器回调 - 用于非阻塞的MoveBoth
void Tc118sEarController::OnStopTimer(TimerHandle_t timer) {
    // 记录停止定时器触发时间
    uint64_t timer_trigger_time_ms = esp_timer_get_time() / 1000;
    if (stop_timer_scheduled_time_ms_ > 0 && scheduled_duration_ms_ > 0) {
        uint64_t timer_delay_ms = timer_trigger_time_ms - stop_timer_scheduled_time_ms_;
        int64_t delay_error_ms = (int64_t)timer_delay_ms - (int64_t)scheduled_duration_ms_;
        
        ESP_LOGI(TAG, "[DURATION] Stop timer triggered: scheduled=%lu ms, actual_delay=%" PRIu64 " ms, error=%" PRId64 " ms", 
                 scheduled_duration_ms_, timer_delay_ms, delay_error_ms);
    }
    
    ESP_LOGI(TAG, "Stop timer triggered - stopping both ears");
    // moving_both_ 在 StopBoth() 中设置，不需要单独设置
    StopBoth();
}

void Tc118sEarController::OnSingleStopTimer(TimerHandle_t timer) {
    StopCtx* ctx = static_cast<StopCtx*>(pvTimerGetTimerID(timer));
    if (!ctx || !ctx->self) return;
    ctx->self->SetGpioLevels(ctx->left, EAR_ACTION_STOP);
}

void Tc118sEarController::UpdateComboState(bool moving, ear_combo_action_t action, uint64_t timestamp_ms) {
    auto apply = [this, moving, action, timestamp_ms]() {
        moving_both_ = moving;
        current_combo_action_ = moving ? action : EAR_COMBO_BOTH_STOP;
        last_combo_start_time_ms_ = moving ? timestamp_ms : 0;
    };

    if (state_mutex_) {
        xSemaphoreTake(state_mutex_, portMAX_DELAY);
        apply();
        xSemaphoreGive(state_mutex_);
    } else {
        apply();
    }
}

void Tc118sEarController::ResetComboState() {
    UpdateComboState(false, EAR_COMBO_BOTH_STOP, 0);
}

void Tc118sEarController::ScheduleComboStop(uint32_t duration_ms) {
    if (duration_ms == 0) {
        return;
    }

    // 记录停止定时器启动时间
    uint64_t timer_start_time_ms = esp_timer_get_time() / 1000;
    
    auto& app = Application::GetInstance();
    if (app.ScheduleEarComboStop(duration_ms)) {
        stop_timer_scheduled_time_ms_ = timer_start_time_ms;
        ESP_LOGD(TAG, "[DURATION] Stop timer scheduled at: %" PRIu64 " ms, duration: %lu ms", 
                 stop_timer_scheduled_time_ms_, duration_ms);
        return;
    }

    if (stop_timer_) {
        stop_timer_scheduled_time_ms_ = timer_start_time_ms;
        ESP_LOGD(TAG, "[DURATION] Stop timer (FreeRTOS) scheduled at: %" PRIu64 " ms, duration: %lu ms", 
                 stop_timer_scheduled_time_ms_, duration_ms);
        xTimerStop(stop_timer_, 0);
        xTimerChangePeriod(stop_timer_, MS_TO_TICKS_MIN1(duration_ms), 0);
        xTimerStart(stop_timer_, 0);
        return;
    }

    StopBoth();
}


// ===== 启动策略实现 =====
void Tc118sEarController::SoftStartSingleEar(bool left_ear, ear_action_t action) {
#if EAR_SOFTSTART_ENABLE
    // 预留：后续可使用 LEDC 对方向有效引脚做占空比渐升
    // 当前占位实现：直接设置 GPIO 水平（等价于无软启动）
#endif
    SetGpioLevels(left_ear, action);
}

void Tc118sEarController::StartBothWithStagger(ear_combo_action_t combo_action, uint32_t duration_ms) {
    (void)duration_ms;

    // 修改：移除vTaskDelay阻塞，改为直接同时启动双耳
    // 错峰逻辑如果硬件确实需要，可以通过PWM软启动实现，但不在Worker Task中阻塞
    // 记录 GPIO 实际设置时间（用于持续时间验证）
    uint64_t gpio_set_time = esp_timer_get_time() / 1000;
    switch (combo_action) {
        case EAR_COMBO_BOTH_FORWARD:
            SoftStartSingleEar(true, EAR_ACTION_FORWARD);
            // 移除阻塞：双耳同时启动，避免阻塞Worker Task
            SoftStartSingleEar(false, EAR_ACTION_FORWARD);
            break;
        case EAR_COMBO_BOTH_BACKWARD:
            SoftStartSingleEar(true, EAR_ACTION_BACKWARD);
            // 移除阻塞：双耳同时启动，避免阻塞Worker Task
            SoftStartSingleEar(false, EAR_ACTION_BACKWARD);
            break;
        default:
            ESP_LOGW(TAG, "StartBothWithStagger: unexpected combo_action=%d", combo_action);
            return;
    }
    // 记录 GPIO 设置完成时间
    gpio_set_time_ms_ = gpio_set_time;
    ESP_LOGD(TAG, "[DURATION] GPIO set at: %" PRIu64 " ms, scheduled duration: %lu ms", 
             gpio_set_time_ms_, scheduled_duration_ms_);
}


