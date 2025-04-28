#pragma once

#include <string>
#include <vector>
#include <functional>

namespace iot {

// 搜索结果结构体
struct MusicSearchResult {
    std::string title;     // 歌曲标题
    std::string artist;    // 艺术家/歌手
    std::string url;       // 歌曲直接播放URL
    std::string cover_url; // 封面URL (可选)
};

// 音乐搜索类
class MusicSearch {
public:
    // 搜索回调函数类型
    using SearchCallback = std::function<void(const std::vector<MusicSearchResult>& results)>;
    
    // 单例模式
    static MusicSearch& GetInstance();
    
    // 搜索音乐
    void SearchMusic(const std::string& query, SearchCallback callback);

private:
    MusicSearch() = default;
    ~MusicSearch() = default;
    
    // 解析HTML内容提取歌曲信息
    bool ParseSearchResults(const std::string& html_content, std::vector<MusicSearchResult>& results);
    
    // 从歌曲详细页面获取实际播放URL
    bool GetPlayUrl(const std::string& detail_url, std::string& play_url);
};

} // namespace iot 