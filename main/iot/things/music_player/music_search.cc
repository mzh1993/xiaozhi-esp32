#include "music_search.h"
#include "background_task.h"
#include "esp_http_client.h"
#include "esp_log.h"

#include <regex>
#include <sstream>
#include <algorithm>
#include <cstring>

#define TAG "MusicSearch"
#define MAX_HTTP_RESPONSE_SIZE 65536
#define MAX_SEARCH_RESULTS 5
#define SEARCH_URL_BASE "https://www.gequhai.com/search/"

namespace iot {

MusicSearch& MusicSearch::GetInstance() {
    static MusicSearch instance;
    return instance;
}

void MusicSearch::SearchMusic(const std::string& query, SearchCallback callback) {
    // 创建后台任务进行搜索，避免阻塞主线程
    auto task = std::make_unique<BackgroundTask>(8192);
    task->Schedule([this, query, callback = std::move(callback), task = std::move(task)]() mutable {
        std::vector<MusicSearchResult> results;
        
        // URL编码查询字符串
        std::string encoded_query;
        for (char c : query) {
            if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                encoded_query += c;
            } else if (c == ' ') {
                encoded_query += '+';
            } else {
                char buf[4];
                snprintf(buf, sizeof(buf), "%%%02X", static_cast<unsigned char>(c));
                encoded_query += buf;
            }
        }
        
        std::string url = SEARCH_URL_BASE + encoded_query;
        ESP_LOGI(TAG, "Searching music: %s, URL: %s", query.c_str(), url.c_str());
        
        // 创建HTTP客户端进行请求
        esp_http_client_config_t config = {
            .url = url.c_str(),
            .method = HTTP_METHOD_GET,
            .timeout_ms = 10000,
        };
        
        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
        
        if (esp_http_client_open(client, 0) != ESP_OK) {
            ESP_LOGE(TAG, "Failed to open HTTP connection");
            esp_http_client_cleanup(client);
            callback(results);  // 返回空结果
            return;
        }
        
        // 读取HTTP响应
        char *buffer = (char *)malloc(MAX_HTTP_RESPONSE_SIZE);
        if (!buffer) {
            ESP_LOGE(TAG, "Failed to allocate memory for HTTP response");
            esp_http_client_cleanup(client);
            callback(results);  // 返回空结果
            return;
        }
        
        int content_length = esp_http_client_fetch_headers(client);
        int read_len = 0;
        
        if (content_length > 0) {
            read_len = esp_http_client_read(client, buffer, std::min(content_length, MAX_HTTP_RESPONSE_SIZE - 1));
        } else {
            // 内容长度未知，分块读取
            int read_index = 0;
            int read_size = 0;
            
            do {
                read_size = esp_http_client_read(client, buffer + read_index, MAX_HTTP_RESPONSE_SIZE - read_index - 1);
                if (read_size > 0) {
                    read_index += read_size;
                }
            } while (read_size > 0 && read_index < MAX_HTTP_RESPONSE_SIZE - 1);
            
            read_len = read_index;
        }
        
        buffer[read_len] = '\0';  // 确保字符串以null结尾
        
        if (read_len > 0) {
            ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %d, read_len = %d",
                esp_http_client_get_status_code(client),
                content_length, read_len);
                
            // 解析搜索结果
            if (ParseSearchResults(buffer, results)) {
                ESP_LOGI(TAG, "Found %d music results", results.size());
                
                // 限制结果数量
                if (results.size() > MAX_SEARCH_RESULTS) {
                    results.resize(MAX_SEARCH_RESULTS);
                }
                
                // 获取每首歌曲的实际播放URL
                for (auto& result : results) {
                    if (!GetPlayUrl(result.url, result.url)) {
                        ESP_LOGW(TAG, "Failed to get play URL for: %s", result.title.c_str());
                    }
                }
            } else {
                ESP_LOGW(TAG, "No music found or failed to parse results");
            }
        } else {
            ESP_LOGE(TAG, "HTTP GET failed, read_len = %d", read_len);
        }
        
        free(buffer);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        
        // 调用回调函数返回结果
        callback(results);
    });
}

bool MusicSearch::ParseSearchResults(const std::string& html_content, std::vector<MusicSearchResult>& results) {
    // 使用正则表达式提取歌曲信息
    // 注意：这是一个简化的解析器，实际网站的HTML结构可能会变化
    // 使用正则表达式匹配歌曲列表项
    std::regex song_pattern("<li class=\"item\">(.*?)</li>");
    std::regex title_pattern("<a.*?class=\"name\".*?href=\"(.*?)\".*?>(.*?)</a>");
    std::regex artist_pattern("<a.*?class=\"singer\".*?>(.*?)</a>");
    
    std::smatch song_match;
    std::string::const_iterator search_start(html_content.cbegin());
    
    while (std::regex_search(search_start, html_content.cend(), song_match, song_pattern)) {
        std::string song_item = song_match[1].str();
        
        MusicSearchResult result;
        
        // 提取歌曲标题和详情页链接
        std::smatch title_match;
        if (std::regex_search(song_item, title_match, title_pattern)) {
            result.url = title_match[1].str();  // 暂存详情页URL
            result.title = title_match[2].str();
            
            // 清理标题中的HTML标签
            result.title = std::regex_replace(result.title, std::regex("<.*?>"), "");
        }
        
        // 提取艺术家
        std::smatch artist_match;
        if (std::regex_search(song_item, artist_match, artist_pattern)) {
            result.artist = artist_match[1].str();
        }
        
        // 只添加有效结果
        if (!result.title.empty() && !result.url.empty()) {
            // 确保URL是完整的
            if (result.url.find("http") != 0) {
                result.url = "https://www.gequhai.com" + result.url;
            }
            results.push_back(result);
        }
        
        search_start = song_match.suffix().first;
    }
    
    return !results.empty();
}

bool MusicSearch::GetPlayUrl(const std::string& detail_url, std::string& play_url) {
    ESP_LOGI(TAG, "Getting play URL from: %s", detail_url.c_str());
    
    // 配置HTTP客户端
    esp_http_client_config_t config = {
        .url = detail_url.c_str(),
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    
    if (esp_http_client_open(client, 0) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection to detail page");
        esp_http_client_cleanup(client);
        return false;
    }
    
    // 读取HTTP响应
    char *buffer = (char *)malloc(MAX_HTTP_RESPONSE_SIZE);
    if (!buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory for HTTP response");
        esp_http_client_cleanup(client);
        return false;
    }
    
    int content_length = esp_http_client_fetch_headers(client);
    int read_len = 0;
    
    if (content_length > 0) {
        read_len = esp_http_client_read(client, buffer, std::min(content_length, MAX_HTTP_RESPONSE_SIZE - 1));
    } else {
        // 内容长度未知，分块读取
        int read_index = 0;
        int read_size = 0;
        
        do {
            read_size = esp_http_client_read(client, buffer + read_index, MAX_HTTP_RESPONSE_SIZE - read_index - 1);
            if (read_size > 0) {
                read_index += read_size;
            }
        } while (read_size > 0 && read_index < MAX_HTTP_RESPONSE_SIZE - 1);
        
        read_len = read_index;
    }
    
    buffer[read_len] = '\0';  // 确保字符串以null结尾
    
    bool success = false;
    
    if (read_len > 0) {
        // 使用正则表达式提取音频URL
        // 示例: data-url="https://example.com/music.mp3" 
        std::regex mp3_url_pattern("data-url=\"(https://[^\"]+\\.mp3)\"");
        std::string html_content(buffer, read_len);
        
        std::smatch mp3_match;
        if (std::regex_search(html_content, mp3_match, mp3_url_pattern)) {
            play_url = mp3_match[1].str();
            ESP_LOGI(TAG, "Found MP3 URL: %s", play_url.c_str());
            success = true;
        } else {
            ESP_LOGW(TAG, "No MP3 URL found in detail page");
        }
    } else {
        ESP_LOGE(TAG, "HTTP GET failed for detail page, read_len = %d", read_len);
    }
    
    free(buffer);
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    
    return success;
}

} // namespace iot 