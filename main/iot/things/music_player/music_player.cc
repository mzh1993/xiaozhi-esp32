// main/iot/things/music_player.cc
#include "music_player.h"
#include "board.h"
#include "audio_codec.h"
#include "background_task.h"
#include "application.h"
#include "protocol.h"
#include "music_search.h"

#include <esp_log.h>
#include <cstring>

// ESP8266Audio库头文件
#include <AudioFileSourceHTTPStream.h>
#include <AudioFileSourceBuffer.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutput.h>

#define TAG "MusicPlayer"

// 缓冲区大小 (32KB)
#define BUFFER_SIZE (32*1024)

// 自定义音频输出类，将解码后的音频数据通过AudioCodec输出
class AudioCodecOutput : public AudioOutput {
public:
    AudioCodecOutput(AudioCodec* codec) : codec_(codec) {}
    
    virtual bool begin() override {
        return true;
    }
    
    virtual bool ConsumeSample(int16_t sample[2]) override {
        sample_buffer_.push_back(sample[0]);  // 仅使用单声道
        
        // 当缓冲区达到足够大小时输出
        if (sample_buffer_.size() >= 480) {  // 30ms @ 16kHz
            codec_->OutputData(sample_buffer_);
            sample_buffer_.clear();
        }
        return true;
    }
    
    virtual bool stop() override {
        // 输出剩余的样本
        if (!sample_buffer_.empty()) {
            codec_->OutputData(sample_buffer_);
            sample_buffer_.clear();
        }
        return true;
    }
    
    virtual bool SetRate(int hz) override {
        // 音频编解码器已经设置了采样率，这里不需要额外处理
        return true;
    }
    
    virtual bool SetBitsPerSample(int bits) override {
        return bits == 16;  // 仅支持16位样本
    }
    
    virtual bool SetChannels(int channels) override {
        return channels == 1;  // 仅支持单声道
    }
    
    virtual bool SetGain(float gain) override {
        // 音量控制通过音频编解码器实现
        return true;
    }
    
    int GetRate() { return 16000; }
    int GetBits() { return 16; }
    int GetChannels() { return 1; }
    
private:
    AudioCodec* codec_;
    std::vector<int16_t> sample_buffer_;
};

namespace iot {

MusicPlayer::MusicPlayer() 
    : Thing("MusicPlayer", "在线音乐播放器")
    , state_(STOPPED)
    , stop_requested_(false)
    , pause_requested_(false)
    , audio_codec_(nullptr) {
    
    InitializeThingProperties();
    InitializeThingMethods();
    
    // 获取音频编解码器实例
    audio_codec_ = Board::GetInstance().GetAudioCodec();
    
    ESP_LOGI(TAG, "MusicPlayer initialized");
}

MusicPlayer::~MusicPlayer() {
    Stop();
}

void MusicPlayer::InitializeThingProperties() {
    // 当前播放标题属性
    properties_.AddStringProperty("title", "当前播放歌曲名称", [this]() -> std::string {
        return current_title_;
    });
    
    // 播放状态属性
    properties_.AddStringProperty("state", "播放状态", [this]() -> std::string {
        switch(state_) {
            case PLAYING: return "playing";
            case PAUSED: return "paused";
            case SEARCHING: return "searching";
            default: return "stopped";
        }
    });
}

void MusicPlayer::InitializeThingMethods() {
    // 播放方法
    methods_.AddMethod("Play", "播放指定URL的音乐", ParameterList({
        Parameter("url", "音频流URL", kValueTypeString, true),
        Parameter("title", "歌曲名称", kValueTypeString, true)
    }), [this](const ParameterList& parameters) {
        PlayUrl(parameters["url"].string(), parameters["title"].string());
    });
    
    // 语音命令播放方法
    methods_.AddMethod("PlayByVoice", "通过语音命令播放音乐", ParameterList({
        Parameter("query", "歌曲搜索关键词", kValueTypeString, true)
    }), [this](const ParameterList& parameters) {
        PlayMusicByVoiceCommand(parameters["query"].string());
    });
    
    // 暂停方法
    methods_.AddMethod("Pause", "暂停当前播放", ParameterList(), 
        [this](const ParameterList& parameters) { Pause(); });
    
    // 继续播放方法
    methods_.AddMethod("Resume", "继续播放", ParameterList(), 
        [this](const ParameterList& parameters) { Resume(); });
    
    // 停止播放方法
    methods_.AddMethod("Stop", "停止播放", ParameterList(), 
        [this](const ParameterList& parameters) { Stop(); });
}

void MusicPlayer::PlayMusicByVoiceCommand(const std::string& song_query) {
    ESP_LOGI(TAG, "Voice command to play music: %s", song_query.c_str());
    
    // 停止当前播放
    Stop();
    
    // 设置为搜索状态
    state_ = SEARCHING;
    current_title_ = "正在搜索: " + song_query;
    
    // 使用MusicSearch类搜索音乐
    MusicSearch::GetInstance().SearchMusic(song_query, [this](const std::vector<MusicSearchResult>& results) {
        HandleSearchResults(results);
    });
}

void MusicPlayer::HandleSearchResults(const std::vector<MusicSearchResult>& results) {
    if (results.empty()) {
        ESP_LOGW(TAG, "No music found for the query");
        current_title_ = "未找到相关音乐";
        state_ = STOPPED;
        return;
    }
    
    // 使用第一个搜索结果播放音乐
    const auto& result = results[0];
    std::string title = result.title;
    if (!result.artist.empty()) {
        title += " - " + result.artist;
    }
    
    ESP_LOGI(TAG, "Playing first result: %s (URL: %s)", title.c_str(), result.url.c_str());
    PlayUrl(result.url, title);
}

void MusicPlayer::PlayUrl(const std::string& url, const std::string& title) {
    // 停止当前正在播放的音频
    Stop();
    
    ESP_LOGI(TAG, "Playing URL: %s, Title: %s", url.c_str(), title.c_str());
    
    current_url_ = url;
    current_title_ = title;
    stop_requested_ = false;
    pause_requested_ = false;
    
    // 确保音频输出已启用
    if (audio_codec_) {
        audio_codec_->EnableOutput(true);
    } else {
        ESP_LOGE(TAG, "AudioCodec not available");
        return;
    }
    
    // 创建HTTP流源
    file_source_ = std::make_unique<AudioFileSourceHTTPStream>(url.c_str());
    
    // 创建缓冲区(32KB缓冲)
    buffered_source_ = std::make_unique<AudioFileSourceBuffer>(file_source_.get(), BUFFER_SIZE);
    
    // 创建MP3解码器
    mp3_decoder_ = std::make_unique<AudioGeneratorMP3>();
    
    // 创建音频输出（自定义回调实现）
    auto audio_output = std::make_unique<AudioCodecOutput>(audio_codec_);
    
    if (!mp3_decoder_->begin(buffered_source_.get(), audio_output.get())) {
        ESP_LOGE(TAG, "Failed to initialize MP3 decoder");
        state_ = STOPPED;
        return;
    }
    
    state_ = PLAYING;
    
    // 创建后台任务进行播放
    player_task_ = std::make_unique<BackgroundTask>(8192);
    player_task_->Schedule([this]() {
        PlaybackLoop();
    });
}

void MusicPlayer::PlaybackLoop() {
    ESP_LOGI(TAG, "Starting audio playback");
    
    // 创建临时输出对象（此对象仅在此函数内有效）
    auto audio_output = std::make_unique<AudioCodecOutput>(audio_codec_);
    
    while (!stop_requested_) {
        if (pause_requested_) {
            // 暂停状态，延迟一段时间再检查
            Application::Sleep(100);
            continue;
        }
        
        if (!mp3_decoder_->isRunning()) {
            // 播放结束或出错
            ESP_LOGI(TAG, "Playback ended");
            break;
        }
        
        if (!mp3_decoder_->loop()) {
            // 解码错误
            ESP_LOGW(TAG, "MP3 decode error");
            break;
        }
        
        // 让出CPU时间给其他任务
        Application::Sleep(10);
    }
    
    // 清理资源
    mp3_decoder_->stop();
    
    ESP_LOGI(TAG, "Playback finished");
    state_ = STOPPED;
}

void MusicPlayer::Pause() {
    if (state_ == PLAYING) {
        ESP_LOGI(TAG, "Pausing playback");
        pause_requested_ = true;
        state_ = PAUSED;
    }
}

void MusicPlayer::Resume() {
    if (state_ == PAUSED) {
        ESP_LOGI(TAG, "Resuming playback");
        pause_requested_ = false;
        state_ = PLAYING;
    }
}

void MusicPlayer::Stop() {
    if (state_ != STOPPED) {
        ESP_LOGI(TAG, "Stopping playback");
        stop_requested_ = true;
        
        // 等待后台任务完成
        if (player_task_) {
            player_task_->WaitForCompletion();
            player_task_.reset();
        }
        
        // 清理资源
        mp3_decoder_.reset();
        buffered_source_.reset();
        file_source_.reset();
        
        state_ = STOPPED;
    }
}

} // namespace iot

// 注册Thing类
DECLARE_THING(MusicPlayer); 