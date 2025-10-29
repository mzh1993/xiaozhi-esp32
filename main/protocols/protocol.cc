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
 * 发送事件消息（已废弃，保留用于兼容）
 * @param message 事件消息
 * 
 * @deprecated 此方法使用 detect 状态发送，服务器已不支持长文本
 * 请使用 SendTouchEvent() 替代
 */
void Protocol::SendMessage(const std::string& message) {
    // 使用 STT 类型发送触摸事件，作为用户输入
    SendTouchEvent(message);
}

/**
 * 发送触摸事件消息
 * @param text 触摸事件的文本内容
 * 
 * 方案说明：
 * 1. 优先使用标准的 listen+detect 格式（与唤醒词相同）
 *    优点：协议标准，服务器一定支持
 *    缺点：如果服务器限制 detect 只能发送短文本，长文本会被拒绝
 * 
 * 2. 备选：如果检测到文本过长，使用 MCP notification
 *    优点：支持长文本，符合 JSON-RPC 2.0 标准
 *    缺点：notifications/touch 是自定义方法名，需要服务器端支持
 * 
 * 注意：notification 不需要服务器响应（无 id 字段），但服务器必须识别该 method 才会处理
 */
void Protocol::SendTouchEvent(const std::string& text) {
    // 对 JSON 字符串进行转义处理
    std::string escaped_text = text;
    size_t pos = 0;
    while ((pos = escaped_text.find('"', pos)) != std::string::npos) {
        escaped_text.replace(pos, 1, "\\\"");
        pos += 2;
    }
    pos = 0;
    while ((pos = escaped_text.find('\n', pos)) != std::string::npos) {
        escaped_text.replace(pos, 1, "\\n");
        pos += 2;
    }

    // 方案1：优先使用标准的 listen+detect（与唤醒词格式一致）
    // 这是协议标准方式，服务器应该支持
    // 如果服务器限制文本长度，会返回错误，此时可以考虑使用方案2
    std::string json = "{\"session_id\":\"" + session_id_ + 
                      "\",\"type\":\"listen\",\"state\":\"detect\",\"text\":\"" + escaped_text + "\"}";
    SendText(json);
    
    // 如果需要使用 MCP notification 方案（长文本），可以取消下面的注释：
    // std::string payload = "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/touch\",\"params\":{\"text\":\"" + 
    //                       escaped_text + "\",\"source\":\"touch\"}}";
    // SendMcpMessage(payload);
}

/**
 * 发送通用事件消息（使用 event 类型，如果服务器支持）
 * @param event_type 事件类型（如 "touch", "button" 等）
 * @param data 事件数据/内容
 * 
 * 作用：发送设备事件给服务器
 * 注意：此方法使用非标准类型，需要服务器支持
 * 建议优先使用 SendTouchEvent() 使用标准的 STT 类型
 */
void Protocol::SendEvent(const std::string& event_type, const std::string& data) {
    // 构造 JSON 消息，包含 session_id、类型、事件类型和数据
    // 需要对 JSON 字符串进行转义处理
    std::string escaped_data = data;
    // 简单的 JSON 转义：将 " 替换为 \"
    size_t pos = 0;
    while ((pos = escaped_data.find('"', pos)) != std::string::npos) {
        escaped_data.replace(pos, 1, "\\\"");
        pos += 2;
    }
    // 处理换行符
    pos = 0;
    while ((pos = escaped_data.find('\n', pos)) != std::string::npos) {
        escaped_data.replace(pos, 1, "\\n");
        pos += 2;
    }
    
    std::string json = "{\"session_id\":\"" + session_id_ + 
                      "\",\"type\":\"event\",\"event\":\"" + event_type + 
                      "\",\"data\":\"" + escaped_data + "\"}";
    SendText(json); // 发送消息
}