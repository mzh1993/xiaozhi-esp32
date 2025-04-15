#ifndef _APPLICATION_H_
#define _APPLICATION_H_

#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/task.h>
#include <esp_timer.h>

#include <string>
#include <mutex>
#include <list>
#include <vector>
#include <condition_variable>

#include <opus_encoder.h>
#include <opus_decoder.h>
#include <opus_resampler.h>

#include "protocol.h"
#include "ota.h"
#include "background_task.h"

#if CONFIG_USE_WAKE_WORD_DETECT
#include "wake_word_detect.h"
#endif
#if CONFIG_USE_AUDIO_PROCESSOR
#include "audio_processor.h"
#endif

<<<<<<< HEAD
// 事件组中的事件位定义
#define SCHEDULE_EVENT (1 << 0)           // 主线程调度事件,表示有新任务需要在主线程中执行
#define AUDIO_INPUT_READY_EVENT (1 << 1)  // 音频输入就绪事件,表示音频输入缓冲区有新数据可读
#define AUDIO_OUTPUT_READY_EVENT (1 << 2) // 音频输出就绪事件,表示音频输出缓冲区可写入新数据
=======
#define SCHEDULE_EVENT (1 << 0)
#define AUDIO_INPUT_READY_EVENT (1 << 1)
#define AUDIO_OUTPUT_READY_EVENT (1 << 2)
#define CHECK_NEW_VERSION_DONE_EVENT (1 << 3)
>>>>>>> dff8f9cb5bf88080db87a66dbed678b7a1f45701

/**
 * @brief 设备状态枚举
 * 
 * 定义了设备可能处于的各种状态:
 * - kDeviceStateUnknown: 未知状态,通常是初始状态
 * - kDeviceStateStarting: 设备正在启动和初始化
 * - kDeviceStateWifiConfiguring: 正在配置WiFi连接
 * - kDeviceStateIdle: 空闲状态,等待用户交互或唤醒
 * - kDeviceStateConnecting: 正在连接服务器
 * - kDeviceStateListening: 正在录音并发送语音到服务器
 * - kDeviceStateSpeaking: 正在播放服务器返回的语音
 * - kDeviceStateUpgrading: 正在进行固件升级
 * - kDeviceStateActivating: 正在激活设备
 * - kDeviceStateFatalError: 发生严重错误
 */
enum DeviceState {
    kDeviceStateUnknown,
    kDeviceStateStarting,
    kDeviceStateWifiConfiguring,
    kDeviceStateIdle,
    kDeviceStateConnecting,
    kDeviceStateListening,
    kDeviceStateSpeaking,
    kDeviceStateUpgrading,
    kDeviceStateActivating,
    kDeviceStateFatalError
};

// Opus音频帧的持续时间,单位为毫秒
// 每个音频帧包含60ms的音频数据,这是Opus编解码器处理音频的基本单位
#define OPUS_FRAME_DURATION_MS 60

/**
 * @class Application
 * @brief 应用程序核心类，管理设备状态、音频处理和通信
 * 
 * 该类使用单例模式实现，负责整个应用程序的生命周期管理，
 * 包括设备状态转换、音频输入输出处理、网络通信等功能。
 */
class Application {
public:
    /**
     * @brief 获取Application单例实例
     * @return Application& 单例实例的引用
     */
    static Application& GetInstance() {
        static Application instance;
        return instance;
    }
    // 删除拷贝构造函数和赋值运算符
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    /**
     * @brief 启动应用程序
     * 
     * 初始化硬件、启动网络连接、设置音频编解码器、
     * 初始化通信协议、启动主循环和后台任务
     */
    void Start();
    
    /**
     * @brief 获取当前设备状态
     * @return DeviceState 当前设备状态枚举值
     */
    DeviceState GetDeviceState() const { return device_state_; }
    
    /**
     * @brief 检查是否检测到语音
     * @return bool 如果检测到语音返回true，否则返回false
     */
    bool IsVoiceDetected() const { return voice_detected_; }
    
    /**
     * @brief 在主线程中调度执行回调函数
     * @param callback 要执行的回调函数
     * 
     * 将回调函数添加到主线程的任务队列中，确保线程安全
     */
    void Schedule(std::function<void()> callback);
    
    /**
     * @brief 设置设备状态
     * @param state 新的设备状态
     * 
     * 更新设备状态，并根据状态变化执行相应的操作，
     * 如更新显示、启动/停止音频处理等
     */
    void SetDeviceState(DeviceState state);
    
    /**
     * @brief 显示警告或提示信息
     * @param status 状态文本
     * @param message 消息内容
     * @param emotion 情感表情，默认为空
     * @param sound 提示音效，默认为空
     * 
     * 在显示器上显示状态和消息，设置情感表情，播放提示音
     */
    void Alert(const char* status, const char* message, const char* emotion = "", const std::string_view& sound = "");
    
    /**
     * @brief 关闭警告或提示信息
     * 
     * 如果设备处于空闲状态，恢复默认显示
     */
    void DismissAlert();
    
    /**
     * @brief 中止语音输出
     * @param reason 中止原因
     * 
     * 停止当前的语音输出，并通知服务器中止原因
     */
    void AbortSpeaking(AbortReason reason);
    
    /**
     * @brief 切换聊天状态
     * 
     * 在空闲、聆听和说话状态之间切换
     */
    void ToggleChatState();
    
    /**
     * @brief 开始聆听用户输入
     * 
     * 打开音频通道，开始接收用户语音输入
     */
    void StartListening();
    
    /**
     * @brief 停止聆听用户输入
     * 
     * 关闭音频通道，停止接收用户语音输入
     */
    void StopListening();
    
    /**
     * @brief 更新IoT设备状态
     * 
     * 收集IoT设备状态并发送到服务器
     */
    void UpdateIotStates();
    
    /**
     * @brief 重启设备
     * 
     * 执行系统重启操作
     */
    void Reboot();
    
    /**
     * @brief 唤醒词触发处理
     * @param wake_word 检测到的唤醒词
     * 
     * 当检测到唤醒词时，根据当前状态执行相应操作
     */
    void WakeWordInvoke(const std::string& wake_word);
    
    /**
     * @brief 播放音效
     * @param sound P3格式的音效数据
     * 
     * 解析P3格式音效数据并播放
     */
    void PlaySound(const std::string_view& sound);
    
    /**
     * @brief 检查是否可以进入睡眠模式
     * @return bool 如果可以进入睡眠模式返回true，否则返回false
     * 
     * 判断当前设备状态是否允许进入低功耗模式
     */
    bool CanEnterSleepMode();

private:
    /**
     * @brief 构造函数
     * 
     * 初始化成员变量，创建事件组和定时器
     */
    Application();
    
    /**
     * @brief 析构函数
     * 
     * 释放资源，停止定时器，删除后台任务
     */
    ~Application();

#if CONFIG_USE_WAKE_WORD_DETECT
    WakeWordDetect wake_word_detect_;    // 唤醒词检测器
#endif
#if CONFIG_USE_AUDIO_PROCESSOR
    AudioProcessor audio_processor_;      // 音频处理器
#endif
<<<<<<< HEAD
    Ota ota_;                            // OTA升级管理器
    std::mutex mutex_;                   // 互斥锁,用于保护共享资源
    std::list<std::function<void()>> main_tasks_;  // 主线程任务队列
    std::unique_ptr<Protocol> protocol_; // 通信协议实现
    EventGroupHandle_t event_group_ = nullptr;      // FreeRTOS事件组句柄
    esp_timer_handle_t clock_timer_handle_ = nullptr;  // ESP定时器句柄
    volatile DeviceState device_state_ = kDeviceStateUnknown;  // 设备当前状态
    bool keep_listening_ = false;        // 是否持续监听音频输入
    bool aborted_ = false;              // 是否中止当前操作
    bool voice_detected_ = false;       // 是否检测到语音输入
    int clock_ticks_ = 0;              // 时钟计数器

    // Audio encode / decode
    BackgroundTask* background_task_ = nullptr;     // 后台任务处理器
    std::chrono::steady_clock::time_point last_output_time_;  // 最后一次音频输出时间
    std::list<std::vector<uint8_t>> audio_decode_queue_;      // 音频解码队列
=======
    Ota ota_;
    std::mutex mutex_;
    std::list<std::function<void()>> main_tasks_;
    std::unique_ptr<Protocol> protocol_;
    EventGroupHandle_t event_group_ = nullptr;
    esp_timer_handle_t clock_timer_handle_ = nullptr;
    volatile DeviceState device_state_ = kDeviceStateUnknown;
    ListeningMode listening_mode_ = kListeningModeAutoStop;
#if CONFIG_USE_REALTIME_CHAT
    bool realtime_chat_enabled_ = true;
#else
    bool realtime_chat_enabled_ = false;
#endif
    bool aborted_ = false;
    bool voice_detected_ = false;
    bool busy_decoding_audio_ = false;
    int clock_ticks_ = 0;
    TaskHandle_t check_new_version_task_handle_ = nullptr;

    // Audio encode / decode
    TaskHandle_t audio_loop_task_handle_ = nullptr;
    BackgroundTask* background_task_ = nullptr;
    std::chrono::steady_clock::time_point last_output_time_;
    std::list<std::vector<uint8_t>> audio_decode_queue_;
    std::condition_variable audio_decode_cv_;
>>>>>>> dff8f9cb5bf88080db87a66dbed678b7a1f45701

    std::unique_ptr<OpusEncoderWrapper> opus_encoder_;    // Opus编码器
    std::unique_ptr<OpusDecoderWrapper> opus_decoder_;    // Opus解码器

<<<<<<< HEAD
    int opus_decode_sample_rate_ = -1;   // Opus解码采样率
    OpusResampler input_resampler_;      // 输入音频重采样器
    OpusResampler reference_resampler_;  // 参考音频重采样器
    OpusResampler output_resampler_;     // 输出音频重采样器

    /**
     * @brief 主循环函数
     * 
     * 处理事件、执行调度的任务、处理音频输入输出
     */
    void MainLoop();
    
    /**
     * @brief 处理音频输入
     * 
     * 从音频编解码器获取输入数据，进行重采样和编码，
     * 发送到服务器或进行唤醒词检测
     */
    void InputAudio();
    
    /**
     * @brief 处理音频输出
     * 
     * 从解码队列获取Opus数据，解码为PCM数据，
     * 进行重采样并输出到音频编解码器
     */
    void OutputAudio();
    
    /**
     * @brief 重置解码器状态
     * 
     * 清空解码队列，重置解码器状态和计时器
     */
    void ResetDecoder();
    
    /**
     * @brief 设置解码采样率
     * @param sample_rate 新的采样率
     * 
     * 更新解码采样率，重新创建解码器，配置重采样器
     */
    void SetDecodeSampleRate(int sample_rate);
    
    /**
     * @brief 检查新版本
     * 
     * 连接服务器检查固件更新，如有新版本则执行升级
     */
=======
    OpusResampler input_resampler_;
    OpusResampler reference_resampler_;
    OpusResampler output_resampler_;

    void MainEventLoop();
    void OnAudioInput();
    void OnAudioOutput();
    void ReadAudio(std::vector<int16_t>& data, int sample_rate, int samples);
    void ResetDecoder();
    void SetDecodeSampleRate(int sample_rate, int frame_duration);
>>>>>>> dff8f9cb5bf88080db87a66dbed678b7a1f45701
    void CheckNewVersion();
    
    /**
     * @brief 显示激活码
     * 
     * 显示并播报设备激活码
     */
    void ShowActivationCode();
    
    /**
     * @brief 时钟定时器回调
     * 
     * 定期执行任务，如打印调试信息、更新时钟显示等
     */
    void OnClockTimer();
    void SetListeningMode(ListeningMode mode);
    void AudioLoop();
};

#endif // _APPLICATION_H_
