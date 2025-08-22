#ifndef _NO_EAR_CONTROLLER_H_
#define _NO_EAR_CONTROLLER_H_

#include "ear_controller.h"

class NoEarController : public EarController {
public:
    NoEarController();
    virtual ~NoEarController();

    // 实现抽象方法
    virtual esp_err_t Initialize() override;
    virtual esp_err_t Deinitialize() override;
    virtual void SetGpioLevels(bool left_ear, ear_action_t action) override;

    // 重写基础控制接口 - 空实现
    virtual esp_err_t MoveEar(bool left_ear, ear_action_param_t action) override;
    virtual esp_err_t StopEar(bool left_ear) override;
    virtual esp_err_t StopBoth() override;

    // 重写双耳组合控制接口 - 空实现
    virtual esp_err_t MoveBoth(ear_combo_param_t combo) override;

    // 重写位置控制接口 - 空实现
    virtual esp_err_t SetEarPosition(bool left_ear, ear_position_t position) override;
    virtual ear_position_t GetEarPosition(bool left_ear) override;
    virtual esp_err_t ResetToDefault() override;

    // 重写序列控制接口 - 空实现
    virtual esp_err_t PlaySequence(const ear_sequence_step_t* steps, uint8_t count, bool loop = false) override;
    virtual esp_err_t StopSequence() override;

    // 重写情绪控制接口 - 空实现
    virtual esp_err_t SetEmotion(const char* emotion, const ear_sequence_step_t* steps, uint8_t count) override;
    virtual esp_err_t TriggerEmotion(const char* emotion) override;
    virtual esp_err_t StopEmotion() override;

    // 重写状态查询接口 - 空实现
    virtual ear_action_t GetCurrentAction(bool left_ear) override;
    virtual bool IsMoving(bool left_ear) override;
    virtual bool IsSequenceActive() override;

private:
    // 私有方法
    void LogOperation(const char* operation, const char* details = nullptr);
};

#endif // _NO_EAR_CONTROLLER_H_
