// main/iot/things/music_player.h
#pragma once

#include "iot/thing.h"
#include "music_search.h"
#include <memory>
#include <string>
#include <vector>

// ESP8266Audio库前向声明
class AudioFileSourceHTTPStream;
class AudioFileSourceBuffer;
class AudioGeneratorMP3;
class AudioCodec;
class BackgroundTask;  // 前向声明

namespace iot {

class MusicPlayer : public Thing {
public:
    MusicPlayer();
    ~MusicPlayer();

    // 播放控制方法
    void PlayUrl(const std::string& url, const std::string& title);
    void Pause();
    void Resume();
    void Stop();
    
    // 语音命令播放音乐
    void PlayMusicByVoiceCommand(const std::string& song_query);

private:
    // 播放状态枚举
    enum PlaybackState {
        STOPPED,
        PLAYING,
        PAUSED,
        SEARCHING  // 新增搜索中状态
    };
    
    // 成员变量
    PlaybackState state_;                      // 当前状态
    std::string current_url_;              // 当前播放URL
    std::string current_title_;            // 当前播放标题
    bool stop_requested_;                  // 停止请求标志
    bool pause_requested_;                 // 暂停请求标志
    std::unique_ptr<BackgroundTask> player_task_;  // 播放任务
    
    // ESP8266Audio组件
    std::unique_ptr<AudioFileSourceHTTPStream> file_source_;    // HTTP音频源
    std::unique_ptr<AudioFileSourceBuffer> buffered_source_;  // 缓冲音频源
    std::unique_ptr<AudioGeneratorMP3> mp3_decoder_;    // MP3解码器
    AudioCodec* audio_codec_;              // 引用AudioCodec实例(不拥有)
    
    // 私有方法
    void InitializeThingProperties();      // 初始化Thing属性
    void InitializeThingMethods();         // 初始化Thing方法
    void PlaybackLoop();                   // MP3播放循环
    void HandleSearchResults(const std::vector<MusicSearchResult>& results);  // 处理搜索结果
};

} // namespace iot 