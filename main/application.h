#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <deque>
#include <memory>

#include "protocol.h"
#include "ota.h"
#include "audio_service.h"
#include "device_state_event.h"


#define MAIN_EVENT_SCHEDULE (1 << 0)
#define MAIN_EVENT_SEND_AUDIO (1 << 1)
#define MAIN_EVENT_WAKE_WORD_DETECTED (1 << 2)
#define MAIN_EVENT_VAD_CHANGE (1 << 3)
#define MAIN_EVENT_ERROR (1 << 4)
#define MAIN_EVENT_CHECK_NEW_VERSION_DONE (1 << 5)
#define MAIN_EVENT_CLOCK_TICK (1 << 6)


enum AecMode {
    kAecOff,
    kAecOnDeviceSide,
    kAecOnServerSide,
};

class Application {
public:
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // 删除拷贝构造函数和赋值运算符
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void Start();
    void MainEventLoop();
    DeviceState GetDeviceState() const { return device_state_; }
    bool IsVoiceDetected() const { return audio_service_.IsVoiceDetected(); }
    void Schedule(std::function<void()> callback);
    void SetDeviceState(DeviceState state);
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    void DismissAlert();
    void AbortSpeaking(AbortReason reason);
    void ToggleChatState();
    void StartListening();
    void StopListening();
    void Reboot();
    void WakeWordInvoke(const std::string& wake_word);
    void PostTouchEvent(const std::string& message);
    void HandleVoiceCommand(const std::string& command);
    bool UpgradeFirmware(Ota& ota, const std::string& url = "");
    bool CanEnterSleepMode();
    void SendMcpMessage(const std::string& payload);
    void SetAecMode(AecMode mode);
    AecMode GetAecMode() const { return aec_mode_; }
    void PlaySound(const std::string_view& sound);
    AudioService& GetAudioService() { return audio_service_; }
    // 外设任务队列访问器
    QueueHandle_t GetPeripheralTaskQueue() { return peripheral_task_queue_; }
    // 外设动作投递（情绪）
    void SchedulePeripheralEmotion(const std::string& emotion);

private:
    Application();
    ~Application();

    std::mutex mutex_;
    std::deque<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    // 触摸超时定时器（非阻塞去监听化）
    esp_timer_handle_t touch_timeout_timer_ = nullptr;
    volatile DeviceState device_state_ = kDeviceStateUnknown;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
    AecMode aec_mode_ = kAecOff;
    std::string last_error_message_;
    AudioService audio_service_;

    bool has_server_time_ = false;
    bool aborted_ = false;
    int clock_ticks_ = 0;
    TaskHandle_t check_new_version_task_handle_ = nullptr;
    TaskHandle_t main_event_loop_task_handle_ = nullptr;
    
    // 音频通道恢复保护：跟踪最后收到 tts start 的时间
    uint64_t last_tts_start_time_ms_ = 0;
    // 最近一次触摸事件时间，用于判断超时与窗口
    uint64_t touch_event_time_ms_ = 0;
    // 首包监控
    bool first_packet_monitoring_ = false;
    uint64_t first_packet_arrival_time_ms_ = 0;
    // 触摸重试：一次简单重试通道
    esp_timer_handle_t touch_retry_timer_ = nullptr;
    std::string pending_touch_message_;
    int touch_retry_attempt_ = 0;
    // speaking中断后延迟处理触摸
    esp_timer_handle_t abort_delay_timer_ = nullptr;
    std::string abort_delay_message_;

    // 外设 Worker
    QueueHandle_t peripheral_task_queue_ = nullptr;
    TaskHandle_t peripheral_worker_task_handle_ = nullptr;
    enum class PeripheralAction {
        kEarEmotion = 0,
        kEarSequence = 1
    };
    struct PeripheralTask {
        PeripheralAction action;
        std::string emotion;
        int combo_action = 0;
        uint32_t duration_ms = 0;
    };

    void OnWakeWordDetected();
    void OnClockTimer();
    void CheckNewVersion(Ota& ota);
    void CheckAssetsVersion();
    void ShowActivationCode(const std::string& code, const std::string& message);
    void SetListeningMode(ListeningMode mode);

    void ProcessTouchEvent(const std::string& message);
    void HandleTouchEventInIdleState(const std::string& message);
    void OnTouchTimeout();
    void OnTouchRetry();
    void OnAbortDelay();
    void PeripheralWorkerTask();
};


class TaskPriorityReset {
public:
    TaskPriorityReset(BaseType_t priority) {
        original_priority_ = uxTaskPriorityGet(NULL);
        vTaskPrioritySet(NULL, priority);
    }
    ~TaskPriorityReset() {
        vTaskPrioritySet(NULL, original_priority_);
    }

private:
    BaseType_t original_priority_;
};

#endif // _APPLICATION_H_
