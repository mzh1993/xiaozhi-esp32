#include "protocol.h"

#include <esp_log.h>

#define TAG "Protocol"

// 设置接收到 JSON 消息时的回调函数
void Protocol::OnIncomingJson(std::function<void(const cJSON* root)> callback) {
    on_incoming_json_ = callback;
}

// 设置接收到音频数据包时的回调函数
void Protocol::OnIncomingAudio(std::function<void(std::unique_ptr<AudioStreamPacket> packet)> callback) {
    on_incoming_audio_ = callback;
}

// 设置音频通道打开时的回调函数
void Protocol::OnAudioChannelOpened(std::function<void()> callback) {
    on_audio_channel_opened_ = callback;
}

// 设置音频通道关闭时的回调函数
void Protocol::OnAudioChannelClosed(std::function<void()> callback) {
    on_audio_channel_closed_ = callback;
}

// 设置网络错误时的回调函数
void Protocol::OnNetworkError(std::function<void(const std::string& message)> callback) {
    on_network_error_ = callback;
}

// 设置错误状态，并触发网络错误回调
void Protocol::OnConnected(std::function<void()> callback) {
    on_connected_ = callback;
}

void Protocol::OnDisconnected(std::function<void()> callback) {
    on_disconnected_ = callback;
}

void Protocol::SetError(const std::string& message) {
    error_occurred_ = true; // 标记已发生错误
    if (on_network_error_ != nullptr) {
        on_network_error_(message); // 调用网络错误回调
    }
}

// 发送“中止说话”消息，reason 用于区分是否因唤醒词打断
void Protocol::SendAbortSpeaking(AbortReason reason) {
    // 构造 JSON 消息，包含 session_id 和类型
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"abort\"";
    // 如果中止原因是唤醒词检测到，则添加 reason 字段
    if (reason == kAbortReasonWakeWordDetected) {
        message += ",\"reason\":\"wake_word_detected\"";
    }
    message += "}";
    SendText(message); // 发送消息
}

// 发送“检测到唤醒词”消息
void Protocol::SendWakeWordDetected(const std::string& wake_word) {
    // 构造 JSON 消息，包含 session_id、类型、状态和唤醒词文本
    std::string json = "{\"session_id\":\"" + session_id_ + 
                      "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + wake_word + "\"}";
    SendText(json); // 发送消息
}

// 发送“开始监听”消息，mode 指定监听模式
void Protocol::SendStartListening(ListeningMode mode) {
    // 构造 JSON 消息，包含 session_id、类型和状态
    std::string message = "{\"session_id\":\"" + session_id_ + "\"";
    message += ",\"type\":\"listen\",\"state\":\"start\"";
    // 根据监听模式添加 mode 字段
    if (mode == kListeningModeRealtime) {
        message += ",\"mode\":\"realtime\"";
    } else if (mode == kListeningModeAutoStop) {
        message += ",\"mode\":\"auto\"";
    } else {
        message += ",\"mode\":\"manual\"";
    }
    message += "}";
    SendText(message); // 发送消息
}

// 发送“停止监听”消息
void Protocol::SendStopListening() {
    // 构造 JSON 消息，包含 session_id、类型和状态
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"listen\",\"state\":\"stop\"}";
    SendText(message); // 发送消息
}

// 发送 MCP 协议消息，payload 为 JSON 字符串
void Protocol::SendMcpMessage(const std::string& payload) {
    // 构造 JSON 消息，包含 session_id、类型和 payload
    std::string message = "{\"session_id\":\"" + session_id_ + "\",\"type\":\"mcp\",\"payload\":" + payload + "}";
    SendText(message); // 发送消息
}

// 判断通道是否超时（无数据包超过指定秒数）
bool Protocol::IsTimeout() const {
    const int kTimeoutSeconds = 120; // 超时时间，单位：秒
    auto now = std::chrono::steady_clock::now(); // 获取当前时间
    // 计算距离上次收到数据的时间间隔
    auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - last_incoming_time_);
    bool timeout = duration.count() > kTimeoutSeconds; // 判断是否超时
    if (timeout) {
        // 打印超时日志
        ESP_LOGE(TAG, "Channel timeout %ld seconds", (long)duration.count());
    }
    return timeout;
}

/**
 * 发送事件消息
 * @param message 事件消息
 * 
 * 作用：通过复用listen消息类型发送事件消息给服务器
 * 事件被标识为特殊的监听模式，服务器可以响应触摸交互
 */
void Protocol::SendMessage(const std::string& message) {
    // 构造 JSON 消息，包含 session_id、类型和状态
    std::string json = "{\"session_id\":\"" + session_id_ + 
    "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + message + "\"}";
    SendText(json); // 发送消息
}