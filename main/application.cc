#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "boards/common/fan_controller.h"
#include "assets.h"
#include "settings.h"

#include <cstring>
#include <memory>
#include <cinttypes>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>
#include <font_awesome.h>

#define TAG "Application"


static const char* const STATE_STRINGS[] = {
    "unknown",
    "starting",
    "configuring",
    "idle",
    "connecting",
    "listening",
    "speaking",
    "upgrading",
    "activating",
    "audio_testing",
    "fatal_error",
    "invalid_state"
};

Application::Application() {
    event_group_ = xEventGroupCreate();

#if CONFIG_USE_DEVICE_AEC && CONFIG_USE_SERVER_AEC
#error "CONFIG_USE_DEVICE_AEC and CONFIG_USE_SERVER_AEC cannot be enabled at the same time"
#elif CONFIG_USE_DEVICE_AEC
    aec_mode_ = kAecOnDeviceSide;
#elif CONFIG_USE_SERVER_AEC
    aec_mode_ = kAecOnServerSide;
#else
    aec_mode_ = kAecOff;
#endif

    esp_timer_create_args_t clock_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            xEventGroupSetBits(app->event_group_, MAIN_EVENT_CLOCK_TICK);
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);

    // 创建触摸超时定时器（用于触摸去监听化的非阻塞超时）
    esp_timer_create_args_t touch_timeout_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnTouchTimeout();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "touch_timeout",
        .skip_unhandled_events = true
    };
    esp_timer_create(&touch_timeout_timer_args, &touch_timeout_timer_);

    // 创建触摸去抖定时器（合并快速连续触摸）
    esp_timer_create_args_t touch_debounce_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnTouchDebounce();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "touch_debounce",
        .skip_unhandled_events = true
    };
    esp_timer_create(&touch_debounce_timer_args, &touch_debounce_timer_);

    // 创建触摸重试定时器
    esp_timer_create_args_t touch_retry_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnTouchRetry();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "touch_retry",
        .skip_unhandled_events = true
    };
    esp_timer_create(&touch_retry_timer_args, &touch_retry_timer_);

    // speaking中断后延迟处理触摸
    esp_timer_create_args_t abort_delay_timer_args = {
        .callback = [](void* arg) {
            Application* app = (Application*)arg;
            app->OnAbortDelay();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "abort_delay",
        .skip_unhandled_events = true
    };
    esp_timer_create(&abort_delay_timer_args, &abort_delay_timer_);

    // 外设任务重试定时器
    esp_timer_create_args_t peripheral_retry_timer_args = {
        .callback = [](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->OnPeripheralRetry();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "peripheral_retry",
        .skip_unhandled_events = true
    };
    esp_timer_create(&peripheral_retry_timer_args, &peripheral_retry_timer_);

    // 耳朵组合动作停止定时器
    esp_timer_create_args_t ear_combo_stop_timer_args = {
        .callback = [](void* arg) {
            Application* app = static_cast<Application*>(arg);
            app->OnEarComboStopTimeout();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "ear_combo_stop",
        .skip_unhandled_events = true
    };
    esp_timer_create(&ear_combo_stop_timer_args, &ear_combo_stop_timer_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    if (touch_timeout_timer_ != nullptr) {
        esp_timer_stop(touch_timeout_timer_);
        esp_timer_delete(touch_timeout_timer_);
        touch_timeout_timer_ = nullptr;
    }
    if (touch_retry_timer_ != nullptr) {
        esp_timer_stop(touch_retry_timer_);
        esp_timer_delete(touch_retry_timer_);
        touch_retry_timer_ = nullptr;
    }
    if (abort_delay_timer_ != nullptr) {
        esp_timer_stop(abort_delay_timer_);
        esp_timer_delete(abort_delay_timer_);
        abort_delay_timer_ = nullptr;
    }
    if (touch_debounce_timer_ != nullptr) {
        esp_timer_stop(touch_debounce_timer_);
        esp_timer_delete(touch_debounce_timer_);
        touch_debounce_timer_ = nullptr;
    }
    if (peripheral_retry_timer_ != nullptr) {
        esp_timer_stop(peripheral_retry_timer_);
        esp_timer_delete(peripheral_retry_timer_);
        peripheral_retry_timer_ = nullptr;
    }
    if (ear_combo_stop_timer_ != nullptr) {
        esp_timer_stop(ear_combo_stop_timer_);
        esp_timer_delete(ear_combo_stop_timer_);
        ear_combo_stop_timer_ = nullptr;
    }
    vEventGroupDelete(event_group_);
}

void Application::CheckAssetsVersion() {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto& assets = Assets::GetInstance();

    if (!assets.partition_valid()) {
        ESP_LOGW(TAG, "Assets partition is disabled for board %s", BOARD_NAME);
        return;
    }
    
    Settings settings("assets", true);
    // Check if there is a new assets need to be downloaded
    std::string download_url = settings.GetString("download_url");

    if (!download_url.empty()) {
        settings.EraseKey("download_url");

        char message[256];
        snprintf(message, sizeof(message), Lang::Strings::FOUND_NEW_ASSETS, download_url.c_str());
        Alert(Lang::Strings::LOADING_ASSETS, message, "cloud_arrow_down", Lang::Sounds::OGG_UPGRADE);
        
        // Wait for the audio service to be idle for 3 seconds
        vTaskDelay(pdMS_TO_TICKS(3000));
        SetDeviceState(kDeviceStateUpgrading);
        board.SetPowerSaveMode(false);
        display->SetChatMessage("system", Lang::Strings::PLEASE_WAIT);

        bool success = assets.Download(download_url, [display](int progress, size_t speed) -> void {
            std::thread([display, progress, speed]() {
                char buffer[32];
                snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
                display->SetChatMessage("system", buffer);
            }).detach();
        });

        board.SetPowerSaveMode(true);
        vTaskDelay(pdMS_TO_TICKS(1000));

        if (!success) {
            Alert(Lang::Strings::ERROR, Lang::Strings::DOWNLOAD_ASSETS_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
            vTaskDelay(pdMS_TO_TICKS(2000));
            return;
        }
    }

    // Apply assets
    assets.Apply();
    display->SetChatMessage("system", "");
    display->SetEmotion("microchip_ai");
}

void Application::CheckNewVersion(Ota& ota) {
    const int MAX_RETRY = 10;
    int retry_count = 0;
    int retry_delay = 10; // 初始重试延迟为10秒

    auto& board = Board::GetInstance();
    while (true) {
        SetDeviceState(kDeviceStateActivating);
        auto display = board.GetDisplay();
        display->SetStatus(Lang::Strings::CHECKING_NEW_VERSION);

        if (!ota.CheckVersion()) {
            retry_count++;
            if (retry_count >= MAX_RETRY) {
                ESP_LOGE(TAG, "Too many retries, exit version check");
                return;
            }

            char buffer[256];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "cloud_slash", Lang::Sounds::OGG_EXCLAMATION);

            ESP_LOGW(TAG, "Check new version failed, retry in %d seconds (%d/%d)", retry_delay, retry_count, MAX_RETRY);
            for (int i = 0; i < retry_delay; i++) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                if (device_state_ == kDeviceStateIdle) {
                    break;
                }
            }
            retry_delay *= 2; // 每次重试后延迟时间翻倍
            continue;
        }
        retry_count = 0;
        retry_delay = 10; // 重置重试延迟时间

        if (ota.HasNewVersion()) {
            if (UpgradeFirmware(ota)) {
                return; // This line will never be reached after reboot
            }
            // If upgrade failed, continue to normal operation (don't break, just fall through)
        }

        // No new version, mark the current version as valid
        ota.MarkCurrentVersionValid();
        if (!ota.HasActivationCode() && !ota.HasActivationChallenge()) {
            xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
            // Exit the loop if done checking new version
            break;
        }

        display->SetStatus(Lang::Strings::ACTIVATION);
        // Activation code is shown to the user and waiting for the user to input
        if (ota.HasActivationCode()) {
            ShowActivationCode(ota.GetActivationCode(), ota.GetActivationMessage());
        }

        // This will block the loop until the activation is done or timeout
        for (int i = 0; i < 10; ++i) {
            ESP_LOGI(TAG, "Activating... %d/%d", i + 1, 10);
            esp_err_t err = ota.Activate();
            if (err == ESP_OK) {
                xEventGroupSetBits(event_group_, MAIN_EVENT_CHECK_NEW_VERSION_DONE);
                break;
            } else if (err == ESP_ERR_TIMEOUT) {
                vTaskDelay(pdMS_TO_TICKS(3000));
            } else {
                vTaskDelay(pdMS_TO_TICKS(10000));
            }
            if (device_state_ == kDeviceStateIdle) {
                break;
            }
        }
    }
}

void Application::ShowActivationCode(const std::string& code, const std::string& message) {
    struct digit_sound {
        char digit;
        const std::string_view& sound;
    };
    static const std::array<digit_sound, 10> digit_sounds{{
        digit_sound{'0', Lang::Sounds::OGG_0},
        digit_sound{'1', Lang::Sounds::OGG_1}, 
        digit_sound{'2', Lang::Sounds::OGG_2},
        digit_sound{'3', Lang::Sounds::OGG_3},
        digit_sound{'4', Lang::Sounds::OGG_4},
        digit_sound{'5', Lang::Sounds::OGG_5},
        digit_sound{'6', Lang::Sounds::OGG_6},
        digit_sound{'7', Lang::Sounds::OGG_7},
        digit_sound{'8', Lang::Sounds::OGG_8},
        digit_sound{'9', Lang::Sounds::OGG_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "link", Lang::Sounds::OGG_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert [%s] %s: %s", emotion, status, message);
    auto display = Board::GetInstance().GetDisplay();
    display->SetStatus(status);
    display->SetEmotion(emotion);
    display->SetChatMessage("system", message);
    if (!sound.empty()) {
        audio_service_.PlaySound(sound);
    }
}

void Application::DismissAlert() {
    if (device_state_ == kDeviceStateIdle) {
        auto display = Board::GetInstance().GetDisplay();
        display->SetStatus(Lang::Strings::STANDBY);
        display->SetEmotion("neutral");
        display->SetChatMessage("system", "");
    }
}

void Application::ToggleChatState() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    } else if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } else if (device_state_ == kDeviceStateListening) {
        Schedule([this]() {
            protocol_->CloseAudioChannel();
        });
    }
}

void Application::StartListening() {
    if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
        return;
    } else if (device_state_ == kDeviceStateWifiConfiguring) {
        audio_service_.EnableAudioTesting(true);
        SetDeviceState(kDeviceStateAudioTesting);
        return;
    }

    if (!protocol_) {
        ESP_LOGE(TAG, "Protocol not initialized");
        return;
    }
    
    if (device_state_ == kDeviceStateIdle) {
        Schedule([this]() {
            if (!protocol_->IsAudioChannelOpened()) {
                SetDeviceState(kDeviceStateConnecting);
                if (!protocol_->OpenAudioChannel()) {
                    return;
                }
            }

            SetListeningMode(kListeningModeManualStop);
        });
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
            SetListeningMode(kListeningModeManualStop);
        });
    }
}

void Application::StopListening() {
    if (device_state_ == kDeviceStateAudioTesting) {
        audio_service_.EnableAudioTesting(false);
        SetDeviceState(kDeviceStateWifiConfiguring);
        return;
    }

    const std::array<int, 3> valid_states = {
        kDeviceStateListening,
        kDeviceStateSpeaking,
        kDeviceStateIdle,
    };
    // If not valid, do nothing
    if (std::find(valid_states.begin(), valid_states.end(), device_state_) == valid_states.end()) {
        return;
    }

    Schedule([this]() {
        if (device_state_ == kDeviceStateListening) {
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
        }
    });
}

void Application::Start() {
    auto& board = Board::GetInstance();
    SetDeviceState(kDeviceStateStarting);

    /* Setup the display */
    auto display = board.GetDisplay();

    // Print board name/version info
    display->SetChatMessage("system", SystemInfo::GetUserAgent().c_str());

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();
    // 注入speaking状态查询，去除音频服务对Application的直接依赖
    audio_service_.SetIsSpeakingQuery([this]() {
        return device_state_ == kDeviceStateSpeaking;
    });

    AudioServiceCallbacks callbacks;
    callbacks.on_send_queue_available = [this]() {
        xEventGroupSetBits(event_group_, MAIN_EVENT_SEND_AUDIO);
    };
    callbacks.on_wake_word_detected = [this](const std::string& wake_word) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_WAKE_WORD_DETECTED);
    };
    callbacks.on_vad_change = [this](bool speaking) {
        xEventGroupSetBits(event_group_, MAIN_EVENT_VAD_CHANGE);
    };
    audio_service_.SetCallbacks(callbacks);

    // Start the main event loop task with priority 3
    xTaskCreate([](void* arg) {
        ((Application*)arg)->MainEventLoop();
        vTaskDelete(NULL);
    }, "main_event_loop", 2048 * 4, this, 3, &main_event_loop_task_handle_);

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new assets version
    CheckAssetsVersion();

    // Check for new firmware version or get the MQTT broker address
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    auto& mcp_server = McpServer::GetInstance();
    mcp_server.AddCommonTools();
    mcp_server.AddUserOnlyTools();

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnConnected([this]() {
        DismissAlert();
    });

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
            // 首包监控：记录 tts start 到首包延迟
            if (first_packet_monitoring_ && first_packet_arrival_time_ms_ == 0) {
                first_packet_arrival_time_ms_ = esp_timer_get_time() / 1000;
                if (last_tts_start_time_ms_ > 0 && first_packet_arrival_time_ms_ >= last_tts_start_time_ms_) {
                    uint64_t elapsed = first_packet_arrival_time_ms_ - last_tts_start_time_ms_;
                    if (elapsed > 3000) {
                        ESP_LOGW(TAG, "First packet delay: %" PRIu64 " ms (>3000)", elapsed);
                    } else {
                        ESP_LOGI(TAG, "First packet delay: %" PRIu64 " ms", elapsed);
                    }
                } else {
                    ESP_LOGW(TAG, "First packet delay: invalid time (tts_start=%" PRIu64 ", arrival=%" PRIu64 ")",
                             last_tts_start_time_ms_, first_packet_arrival_time_ms_);
                }
                first_packet_monitoring_ = false;
            }
            audio_service_.PushPacketToDecodeQueue(std::move(packet));
        }
    });
    protocol_->OnAudioChannelOpened([this, codec, &board]() {
        board.SetPowerSaveMode(false);
        if (protocol_->server_sample_rate() != codec->output_sample_rate()) {
            ESP_LOGW(TAG, "Server sample rate %d does not match device output sample rate %d, resampling may cause distortion",
                protocol_->server_sample_rate(), codec->output_sample_rate());
        }
    });
    protocol_->OnAudioChannelClosed([this, &board]() {
        board.SetPowerSaveMode(true);
        Schedule([this]() {
            // 检查是否是 speaking 状态或最近收到 tts start（5秒内）
            // 如果是，尝试重新打开音频通道而不是直接切到 idle
            uint64_t current_time_ms = esp_timer_get_time() / 1000;
            bool recent_tts_start = (last_tts_start_time_ms_ > 0 && 
                                     (current_time_ms - last_tts_start_time_ms_) < 5000);
            
            if (device_state_ == kDeviceStateSpeaking || recent_tts_start) {
                ESP_LOGW(TAG, "Audio channel closed during speaking or recent tts start, attempting to reopen");
                
                // 尝试重新打开音频通道
                if (protocol_ && protocol_->OpenAudioChannel()) {
                    ESP_LOGI(TAG, "Audio channel reopened successfully after unexpected close");
                    
                    // 确保音频输出启用
                    auto codec = Board::GetInstance().GetAudioCodec();
                    if (codec) {
                        codec->EnableOutput(true);
                        ESP_LOGI(TAG, "Audio output re-enabled after channel reopen");
                    }
                    
                    // 保持当前状态，不切换到 idle
                    return;
                } else {
                    ESP_LOGW(TAG, "Failed to reopen audio channel, will switch to idle");
                }
            }
            
            // 如果不是 speaking 状态或重开失败，正常处理通道关闭
            auto display = Board::GetInstance().GetDisplay();
            display->SetChatMessage("system", "");
            SetDeviceState(kDeviceStateIdle);
        });
    });
    protocol_->OnIncomingJson([this, display](const cJSON* root) {
        // Parse JSON data
        auto type = cJSON_GetObjectItem(root, "type");
        if (strcmp(type->valuestring, "tts") == 0) {
            auto state = cJSON_GetObjectItem(root, "state");
            if (strcmp(state->valuestring, "start") == 0) {
                Schedule([this]() {
                    aborted_ = false;
                    
                    // 记录 tts start 时间，用于通道关闭保护
                    last_tts_start_time_ms_ = esp_timer_get_time() / 1000;
                    // 开启首包监控
                    first_packet_monitoring_ = true;
                    first_packet_arrival_time_ms_ = 0;

                    // 取消触摸超时定时器（若仍在等待）
                    if (touch_timeout_timer_) {
                        // 检查定时器是否真的在运行（避免唤醒词场景下的误导日志）
                        uint64_t expiry_time = 0;
                        if (esp_timer_get_expiry_time(touch_timeout_timer_, &expiry_time) == ESP_OK) {
                            int64_t remaining = expiry_time - esp_timer_get_time();
                            if (remaining > 0) {
                                esp_timer_stop(touch_timeout_timer_);
                                ESP_LOGI(TAG, "Touch timeout cancelled by tts start");
                            }
                        }
                    }

                    // 重置触摸重试状态
                    touch_retry_attempt_ = 0;
                    pending_touch_message_.clear();
                    if (touch_retry_timer_) {
                        esp_timer_stop(touch_retry_timer_);
                    }
                    // 取消speaking中断延迟处理（如仍在排队）
                    abort_delay_message_.clear();
                    if (abort_delay_timer_) {
                        esp_timer_stop(abort_delay_timer_);
                    }
                    // 重置连续超时计数（tts start成功，说明网络正常）
                    consecutive_touch_timeouts_ = 0;
                    // 如果保护模式还在，但tts start成功，可以提前退出保护模式
                    if (direct_speaking_protection_mode_) {
                        direct_speaking_protection_mode_ = false;
                        ESP_LOGI(TAG, "Direct speaking protection mode disabled (tts start received)");
                    }
                    
                    // 强制确保音频通道打开（可能在状态切换过程中被关闭）
                    if (!protocol_ || !protocol_->IsAudioChannelOpened()) {
                        ESP_LOGW(TAG, "Audio channel closed when tts start received, reopening...");
                        if (!protocol_) {
                            ESP_LOGE(TAG, "Protocol not initialized");
                            return;
                        }
                        SetDeviceState(kDeviceStateConnecting);
                        if (!protocol_->OpenAudioChannel()) {
                            ESP_LOGE(TAG, "Failed to reopen audio channel for tts start");
                            SetDeviceState(kDeviceStateIdle);
                            return;
                        }
                        ESP_LOGI(TAG, "Audio channel reopened successfully for tts start");
                    }
                    
                    // 刷新输出时间，首包保护
                    audio_service_.RefreshLastOutputTime();

                    // 强制确保音频输出启用
                    auto codec = Board::GetInstance().GetAudioCodec();
                    if (codec) {
                        codec->EnableOutput(true);
                        ESP_LOGI(TAG, "Audio output enabled for tts start");
                    }
                    
                    // 放宽状态检查：允许在listening、idle或connecting时切换到speaking
                    // 这样可以处理触摸事件触发的语音响应
                    if (device_state_ == kDeviceStateIdle || 
                        device_state_ == kDeviceStateListening ||
                        device_state_ == kDeviceStateConnecting) {
                        SetDeviceState(kDeviceStateSpeaking);
                    } else if (device_state_ == kDeviceStateSpeaking) {
                        // 如果已经是speaking状态，确保音频输出启用
                        if (codec) {
                            codec->EnableOutput(true);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "stop") == 0) {
                Schedule([this]() {
                    if (device_state_ == kDeviceStateSpeaking) {
                        if (listening_mode_ == kListeningModeManualStop) {
                            SetDeviceState(kDeviceStateIdle);
                        } else {
                            SetDeviceState(kDeviceStateListening);
                        }
                    }
                });
            } else if (strcmp(state->valuestring, "sentence_start") == 0) {
                auto text = cJSON_GetObjectItem(root, "text");
                if (cJSON_IsString(text)) {
                    ESP_LOGI(TAG, "<< %s", text->valuestring);
                    Schedule([this, display, message = std::string(text->valuestring)]() {
                        display->SetChatMessage("assistant", message.c_str());
                        
                        // 收到句子开始，强制确保音频输出启用（防止音频输出被意外禁用）
                        auto codec = Board::GetInstance().GetAudioCodec();
                        if (codec) {
                            codec->EnableOutput(true);
                        }
                        
                        // 确保音频通道仍然打开
                        if (protocol_ && !protocol_->IsAudioChannelOpened()) {
                            ESP_LOGW(TAG, "Audio channel closed during sentence_start, attempting to reopen");
                            if (protocol_->OpenAudioChannel()) {
                                ESP_LOGI(TAG, "Audio channel reopened during sentence_start");
                            }
                        }
                    });
                }
            }
        } else if (strcmp(type->valuestring, "stt") == 0) {
            auto text = cJSON_GetObjectItem(root, "text");
            if (cJSON_IsString(text)) {
                ESP_LOGI(TAG, ">> %s", text->valuestring);
                Schedule([this, display, message = std::string(text->valuestring)]() {
                    display->SetChatMessage("user", message.c_str());
                });
            }
        } else if (strcmp(type->valuestring, "llm") == 0) {
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(emotion)) {
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    // 更新显示
                    display->SetEmotion(emotion_str.c_str());
                    
                    // 触发耳朵动作（让耳朵控制器自己处理重复检查）
                    auto ear_controller = Board::GetInstance().GetEarController();
                    if (ear_controller) {
                        ear_controller->TriggerEmotion(emotion_str.c_str());
                    }
                });
            }
        } else if (strcmp(type->valuestring, "mcp") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            if (cJSON_IsObject(payload)) {
                McpServer::GetInstance().ParseMessage(payload);
            }
        } else if (strcmp(type->valuestring, "system") == 0) {
            auto command = cJSON_GetObjectItem(root, "command");
            if (cJSON_IsString(command)) {
                ESP_LOGI(TAG, "System command: %s", command->valuestring);
                if (strcmp(command->valuestring, "reboot") == 0) {
                    // Do a reboot if user requests a OTA update
                    Schedule([this]() {
                        Reboot();
                    });
                } else {
                    ESP_LOGW(TAG, "Unknown system command: %s", command->valuestring);
                }
            }
        } else if (strcmp(type->valuestring, "alert") == 0) {
            auto status = cJSON_GetObjectItem(root, "status");
            auto message = cJSON_GetObjectItem(root, "message");
            auto emotion = cJSON_GetObjectItem(root, "emotion");
            if (cJSON_IsString(status) && cJSON_IsString(message) && cJSON_IsString(emotion)) {
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::OGG_VIBRATION);
            } else {
                ESP_LOGW(TAG, "Alert command requires status, message and emotion");
            }
#if CONFIG_RECEIVE_CUSTOM_MESSAGE
        } else if (strcmp(type->valuestring, "custom") == 0) {
            auto payload = cJSON_GetObjectItem(root, "payload");
            ESP_LOGI(TAG, "Received custom message: %s", cJSON_PrintUnformatted(root));
            if (cJSON_IsObject(payload)) {
                Schedule([this, display, payload_str = std::string(cJSON_PrintUnformatted(payload))]() {
                    display->SetChatMessage("system", payload_str.c_str());
                });
            } else {
                ESP_LOGW(TAG, "Invalid custom message format: missing payload");
            }
#endif
        } else {
            ESP_LOGW(TAG, "Unknown message type: %s", type->valuestring);
        }
    });
    bool protocol_started = protocol_->Start();

    SystemInfo::PrintHeapStats();
    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::OGG_SUCCESS);
    }

    // Initialize ear controller emotion mappings
    auto ear_controller = Board::GetInstance().GetEarController();
    ESP_LOGI(TAG, "Getting ear controller for emotion mapping initialization: %s", ear_controller ? "valid" : "null");
    
    // Print heap stats
    SystemInfo::PrintHeapStats(); 

    // 创建外设 Worker（队列 + 任务）
    if (peripheral_task_queue_ == nullptr) {
        peripheral_queue_length_ = 16;
        peripheral_task_queue_ = xQueueCreate(peripheral_queue_length_, sizeof(PeripheralTask*));
    }
    if (peripheral_worker_task_handle_ == nullptr && peripheral_task_queue_ != nullptr) {
        xTaskCreate([](void* arg){
            static_cast<Application*>(arg)->PeripheralWorkerTask();
        }, "peripheral_worker", 2048, this, 5, &peripheral_worker_task_handle_);
    }
}

void Application::OnClockTimer() {
    clock_ticks_++;

    auto display = Board::GetInstance().GetDisplay();
    display->UpdateStatusBar();

    // Print the debug info every 10 seconds
    if (clock_ticks_ % 10 == 0) {
        // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
        // SystemInfo::PrintTaskList();
        SystemInfo::PrintHeapStats();
        if (peripheral_task_queue_ != nullptr && peripheral_queue_length_ > 0) {
            size_t current_usage = GetPeripheralQueueUsage();
            ESP_LOGI(TAG, "Peripheral queue usage: %zu/%u, max=%zu, retry=%u, drop=%u",
                current_usage, peripheral_queue_length_,
                peripheral_queue_max_usage_.load(),
                peripheral_queue_retry_count_.load(),
                peripheral_queue_drop_count_.load());
        }
    }
}

void Application::OnTouchTimeout() {
    // 在定时器任务上下文，使用主循环串行处理
    Schedule([this]() {
        // 若已进入 speaking，忽略超时
        if (device_state_ == kDeviceStateSpeaking) {
            ESP_LOGI(TAG, "Touch timeout: already speaking, ignore");
            // 成功进入speaking，复位重试状态
            touch_retry_attempt_ = 0;
            pending_touch_message_.clear();
            if (touch_retry_timer_) esp_timer_stop(touch_retry_timer_);
            return;
        }

        // 若刚刚收到 tts start（≤1s），再等 1s
        uint64_t now_ms = esp_timer_get_time() / 1000;
        bool recent_tts = (last_tts_start_time_ms_ > 0 &&
                           last_tts_start_time_ms_ >= touch_event_time_ms_ &&
                           (now_ms - last_tts_start_time_ms_) < 1000);
        if (recent_tts) {
            if (touch_timeout_timer_) {
                esp_timer_start_once(touch_timeout_timer_, 1000000);
                ESP_LOGI(TAG, "Touch timeout deferred by 1s due to recent tts start");
            }
            return;
        }

        // 超时回退到 listening
        ESP_LOGW(TAG, "Touch timeout reached, entering listening");
        consecutive_touch_timeouts_++;
        ESP_LOGW(TAG, "Consecutive touch timeouts: %d", consecutive_touch_timeouts_);
        
        // 连续2-3次超时后启用保护模式（跳过listen+start，直接speaking保护）
        if (consecutive_touch_timeouts_ >= 2 && !direct_speaking_protection_mode_) {
            direct_speaking_protection_mode_ = true;
            uint64_t now_ms = esp_timer_get_time() / 1000;
            protection_mode_until_ms_ = now_ms + 60000; // 保护模式持续60秒
            ESP_LOGW(TAG, "Direct speaking protection mode enabled (60s)");
        }
        
        SetListeningMode(kListeningModeAutoStop);
        // 回退后复位重试状态
        touch_retry_attempt_ = 0;
        pending_touch_message_.clear();
        if (touch_retry_timer_) esp_timer_stop(touch_retry_timer_);
    });
}

void Application::OnTouchRetry() {
    // 若超过最大重试次数，放弃
    if (touch_retry_attempt_ >= 5) {
        ESP_LOGW(TAG, "Touch retry exceeded max attempts, dropping: %s", pending_touch_message_.c_str());
        touch_retry_attempt_ = 0;
        pending_touch_message_.clear();
        return;
    }
    std::string message = pending_touch_message_;
    if (message.empty()) {
        return;
    }
    Schedule([this, message]() {
        ESP_LOGI(TAG, "Retrying touch event: %s", message.c_str());
        ProcessTouchEvent(message);
    });
}

void Application::OnAbortDelay() {
    std::string message = abort_delay_message_;
    if (message.empty()) return;
    Schedule([this, message]() {
        HandleTouchEventInIdleState(message);
    });
    abort_delay_message_.clear();
}

void Application::OnTouchDebounce() {
    std::string message = debounced_touch_message_;
    if (message.empty()) return;
    // 若与上次处理的触摸在200ms窗口内且相同，丢弃
    uint64_t now_ms = esp_timer_get_time() / 1000;
    if (!last_processed_touch_message_.empty() &&
        message == last_processed_touch_message_ &&
        (now_ms - last_processed_touch_time_ms_) <= 200) {
        ESP_LOGI(TAG, "Debounced duplicate touch: %s", message.c_str());
        return;
    }
    Schedule([this, message]() {
        last_processed_touch_message_ = message;
        last_processed_touch_time_ms_ = esp_timer_get_time() / 1000;
        ProcessTouchEvent(message);
    });
}

size_t Application::GetPeripheralQueueUsage() const {
    if (peripheral_task_queue_ == nullptr || peripheral_queue_length_ == 0) {
        return 0;
    }
    UBaseType_t spaces = uxQueueSpacesAvailable(peripheral_task_queue_);
    if (spaces > peripheral_queue_length_) {
        return 0;
    }
    return static_cast<size_t>(peripheral_queue_length_ - spaces);
}

void Application::SchedulePeripheralRetry(uint32_t delay_us) {
    if (peripheral_retry_timer_ == nullptr) {
        return;
    }
    esp_timer_stop(peripheral_retry_timer_);
    esp_timer_start_once(peripheral_retry_timer_, delay_us);
}

void Application::OnPeripheralRetry() {
    std::deque<std::unique_ptr<PeripheralTask>> pending;
    {
        std::lock_guard<std::mutex> lock(peripheral_retry_mutex_);
        size_t count = peripheral_retry_queue_.size();
        for (size_t i = 0; i < count; ++i) {
            pending.push_back(std::move(peripheral_retry_queue_.front()));
            peripheral_retry_queue_.pop_front();
        }
    }

    for (auto& task : pending) {
        EnqueuePeripheralTask(std::move(task), 0, true);
    }

    bool has_more = false;
    {
        std::lock_guard<std::mutex> lock(peripheral_retry_mutex_);
        has_more = !peripheral_retry_queue_.empty();
    }

    if (has_more) {
        SchedulePeripheralRetry(kPeripheralRetryDelayUs);
    }
}

bool Application::ScheduleEarComboStop(uint32_t duration_ms) {
    if (ear_combo_stop_timer_ == nullptr || duration_ms == 0) {
        return false;
    }
    esp_timer_stop(ear_combo_stop_timer_);
    esp_timer_start_once(ear_combo_stop_timer_, duration_ms * 1000ULL);
    return true;
}

void Application::CancelEarComboStopTimer() {
    if (ear_combo_stop_timer_) {
        esp_timer_stop(ear_combo_stop_timer_);
    }
}

void Application::OnEarComboStopTimeout() {
    auto task = std::make_unique<PeripheralTask>();
    task->action = PeripheralAction::kEarStopCombo;
    task->source = PeripheralTaskSource::kSequence;
    if (!EnqueuePeripheralTask(std::move(task))) {
        ESP_LOGW(TAG, "Failed to enqueue ear combo stop task");
    }
}

void Application::SchedulePeripheralEmotion(const std::string& emotion) {
    if (peripheral_task_queue_ == nullptr) return;
    auto task = std::make_unique<PeripheralTask>();
    task->action = PeripheralAction::kEarEmotion;
    task->emotion = emotion;
    task->source = PeripheralTaskSource::kEmotion;
    if (!EnqueuePeripheralTask(std::move(task))) {
        ESP_LOGW(TAG, "Failed to enqueue peripheral emotion task: %s", emotion.c_str());
    }
}

void Application::PeripheralWorkerTask() {
    PeripheralTask* task = nullptr;
    while (true) {
        if (xQueueReceive(peripheral_task_queue_, &task, portMAX_DELAY) == pdTRUE) {
            std::unique_ptr<PeripheralTask> task_ptr(task);
            if (!task_ptr) {
                continue;
            }
            // speaking 首包窗口期（2s）抑制大动作 - 改为非阻塞延迟投递
            // 注意：停止动作（kEarStopCombo）应立即执行，不应延迟
            uint64_t now_ms = esp_timer_get_time() / 1000;
            bool should_delay = (task_ptr->action != PeripheralAction::kEarStopCombo) &&
                               device_state_ == kDeviceStateSpeaking &&
                               last_tts_start_time_ms_ > 0 &&
                               (now_ms - last_tts_start_time_ms_) < 2000;
            
            if (should_delay) {
                uint64_t remain = 2000 - (now_ms - last_tts_start_time_ms_);
                // 将任务重新放回队列，延迟执行，避免阻塞Worker Task
                // 使用定时器延迟投递，而不是阻塞当前任务
                if (peripheral_retry_timer_) {
                    {
                        std::lock_guard<std::mutex> lock(peripheral_retry_mutex_);
                        peripheral_retry_queue_.push_back(std::move(task_ptr));
                    }
                    esp_timer_stop(peripheral_retry_timer_);
                    esp_timer_start_once(peripheral_retry_timer_, remain * 1000ULL);
                    ESP_LOGD(TAG, "Delaying peripheral action in speaking first packet window: %llu ms", remain);
                    continue;  // 跳过当前任务，等待延迟后重新投递
                }
                // 如果定时器不可用，允许执行（总比阻塞好）
            }
            
            auto ear = Board::GetInstance().GetEarController();
            switch (task_ptr->action) {
                case PeripheralAction::kEarEmotion:
                    if (ear) ear->TriggerEmotion(task_ptr->emotion.c_str());
                    break;
                case PeripheralAction::kEarSequence: {
                    if (ear) {
                        ear_combo_param_t combo;
                        combo.combo_action = static_cast<ear_combo_action_t>(task_ptr->combo_action);
                        combo.duration_ms = task_ptr->duration_ms;
                        ear->MoveBoth(combo);
                    }
                    break;
                }
                case PeripheralAction::kEarStopCombo:
                    // 停止动作应立即执行，不应延迟
                    if (ear) {
                        ear->StopBoth();
                    }
                    break;
                default:
                    break;
            }
        }
    }
}

// Add a async task to MainLoop
void Application::Schedule(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        main_tasks_.push_back(std::move(callback));
    }
    xEventGroupSetBits(event_group_, MAIN_EVENT_SCHEDULE);
}

bool Application::EnqueuePeripheralTask(std::unique_ptr<PeripheralTask> task, TickType_t ticks_to_wait, bool allow_retry) {
    if (peripheral_task_queue_ == nullptr || !task) {
        return false;
    }

    if (peripheral_queue_length_ == 0 && peripheral_task_queue_ != nullptr) {
        peripheral_queue_length_ = uxQueueMessagesWaiting(peripheral_task_queue_) + uxQueueSpacesAvailable(peripheral_task_queue_);
    }

    PeripheralTask* raw_task = task.get();
    if (xQueueSend(peripheral_task_queue_, &raw_task, ticks_to_wait) == pdTRUE) {
        size_t current_usage = GetPeripheralQueueUsage();
        size_t previous = peripheral_queue_max_usage_.load();
        while (current_usage > previous &&
               !peripheral_queue_max_usage_.compare_exchange_weak(previous, current_usage)) {
        }
        task.release();
        return true;
    }

    if (!allow_retry) {
        peripheral_queue_drop_count_.fetch_add(1);
        ESP_LOGW(TAG, "Peripheral queue drop (action=%d, source=%d)",
                 static_cast<int>(task->action), static_cast<int>(task->source));
        return false;
    }

    if (task->retry_count < kPeripheralMaxRetry) {
        task->retry_count++;
        peripheral_queue_retry_count_.fetch_add(1);
        {
            std::lock_guard<std::mutex> lock(peripheral_retry_mutex_);
            peripheral_retry_queue_.push_back(std::move(task));
        }
        SchedulePeripheralRetry();
    } else {
        peripheral_queue_drop_count_.fetch_add(1);
        ESP_LOGW(TAG, "Peripheral queue drop after retries (action=%d, source=%d)",
                 static_cast<int>(task->action), static_cast<int>(task->source));
    }

    return false;
}

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_CLOCK_TICK |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (protocol_ && !protocol_->SendAudio(std::move(packet))) {
                    break;
                }
            }
        }

        if (bits & MAIN_EVENT_WAKE_WORD_DETECTED) {
            OnWakeWordDetected();
        }

        if (bits & MAIN_EVENT_VAD_CHANGE) {
            if (device_state_ == kDeviceStateListening) {
                auto led = Board::GetInstance().GetLed();
                led->OnStateChanged();
            }
        }

        if (bits & MAIN_EVENT_SCHEDULE) {
            std::unique_lock<std::mutex> lock(mutex_);
            auto tasks = std::move(main_tasks_);
            lock.unlock();
            for (auto& task : tasks) {
                task();
            }
        }

        if (bits & MAIN_EVENT_CLOCK_TICK) {
            clock_ticks_++;
            auto display = Board::GetInstance().GetDisplay();
            display->UpdateStatusBar();
        
            // Print the debug info every 10 seconds
            if (clock_ticks_ % 10 == 0) {
                // SystemInfo::PrintTaskCpuUsage(pdMS_TO_TICKS(1000));
                // SystemInfo::PrintTaskList();
                SystemInfo::PrintHeapStats();
            }
        }
    }
}

void Application::OnWakeWordDetected() {
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        auto wake_word = audio_service_.GetLastWakeWord();
        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_SEND_WAKE_WORD_DATA
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        AbortSpeaking(kAbortReasonWakeWordDetected);
    } else if (device_state_ == kDeviceStateActivating) {
        SetDeviceState(kDeviceStateIdle);
    }
}

void Application::AbortSpeaking(AbortReason reason) {
    ESP_LOGI(TAG, "Abort speaking");
    aborted_ = true;
    if (protocol_) {
        protocol_->SendAbortSpeaking(reason);
    }
}

void Application::SetListeningMode(ListeningMode mode) {
    ESP_LOGI(TAG, "SetListeningMode called with mode: %d", mode);
    listening_mode_ = mode;
    ESP_LOGI(TAG, "About to call SetDeviceState(kDeviceStateListening)");
    SetDeviceState(kDeviceStateListening);
    ESP_LOGI(TAG, "SetListeningMode completed");
}

void Application::SetDeviceState(DeviceState state) {
    if (device_state_ == state) {
        return;
    }
    
    clock_ticks_ = 0;
    auto previous_state = device_state_;
    device_state_ = state;
    ESP_LOGI(TAG, "STATE: %s", STATE_STRINGS[device_state_]);

    // Send the state change event
    DeviceStateEventManager::GetInstance().PostStateChangeEvent(previous_state, state);

    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    auto led = board.GetLed();
    led->OnStateChanged();
    
    // 获取耳朵控制器
    auto ear_controller = board.GetEarController();
    
    switch (state) {
        case kDeviceStateUnknown:
        case kDeviceStateIdle:
            display->SetStatus(Lang::Strings::STANDBY);
            display->SetEmotion("neutral");
            audio_service_.EnableVoiceProcessing(false);
            audio_service_.EnableWakeWordDetection(true);
            
            // 空闲状态时确保耳朵下垂
            if (ear_controller) {
                // 等待序列完成，避免冲突
                if (ear_controller->IsSequenceActive()) {
                    ESP_LOGI(TAG, "Sequence active, skipping ear reset to avoid conflict");
                } else {
                    ESP_LOGI(TAG, "Device entering idle state, ensuring ears are down");
                    ear_controller->SetEarInitialPosition();
                }
            }
            break;
        case kDeviceStateConnecting:
            display->SetStatus(Lang::Strings::CONNECTING);
            display->SetEmotion("neutral");
            display->SetChatMessage("system", "");
            break;
        case kDeviceStateListening:
            ESP_LOGI(TAG, "Entering kDeviceStateListening state");
            display->SetStatus(Lang::Strings::LISTENING);
            display->SetEmotion("neutral");

            // Make sure the audio processor is running
            ESP_LOGI(TAG, "Checking if audio processor is running: %s", audio_service_.IsAudioProcessorRunning() ? "true" : "false");
            if (!audio_service_.IsAudioProcessorRunning()) {
                ESP_LOGI(TAG, "Audio processor not running, starting it now");
                // Send the start listening command
                ESP_LOGI(TAG, "Sending start listening command with mode: %d", listening_mode_);
                protocol_->SendStartListening(listening_mode_);
                ESP_LOGI(TAG, "About to call audio_service_.EnableVoiceProcessing(true)");
                audio_service_.EnableVoiceProcessing(true);
                ESP_LOGI(TAG, "audio_service_.EnableVoiceProcessing(true) completed");
                ESP_LOGI(TAG, "About to call audio_service_.EnableWakeWordDetection(false)");
                audio_service_.EnableWakeWordDetection(false);
                ESP_LOGI(TAG, "audio_service_.EnableWakeWordDetection(false) completed");
                ESP_LOGI(TAG, "Audio processor should now be running");
            } else {
                ESP_LOGI(TAG, "Audio processor is already running, skipping initialization");
            }
            ESP_LOGI(TAG, "kDeviceStateListening state setup completed");
            break;
        case kDeviceStateSpeaking:
        {
            ESP_LOGI(TAG, "Entering kDeviceStateSpeaking state");
            display->SetStatus(Lang::Strings::SPEAKING);

            // 强制确保音频输出启用（speaking 状态必须启用音频输出）
            auto speaking_codec = Board::GetInstance().GetAudioCodec();
            if (speaking_codec) {
                speaking_codec->EnableOutput(true);
                ESP_LOGI(TAG, "Audio output enabled for speaking state");
            }

            if (listening_mode_ != kListeningModeRealtime) {
                ESP_LOGI(TAG, "listening_mode_ != kListeningModeRealtime, disabling voice processing");
                ESP_LOGI(TAG, "About to call audio_service_.EnableVoiceProcessing(false)");
                audio_service_.EnableVoiceProcessing(false);
                ESP_LOGI(TAG, "audio_service_.EnableVoiceProcessing(false) completed");
                // Only AFE wake word can be detected in speaking mode
#if CONFIG_USE_AFE_WAKE_WORD
                ESP_LOGI(TAG, "CONFIG_USE_AFE_WAKE_WORD enabled, enabling wake word detection");
                audio_service_.EnableWakeWordDetection(true);
#else
                ESP_LOGI(TAG, "CONFIG_USE_AFE_WAKE_WORD disabled, disabling wake word detection");
                audio_service_.EnableWakeWordDetection(false);
#endif
            } else {
                ESP_LOGI(TAG, "listening_mode_ == kListeningModeRealtime, keeping voice processing enabled");
                audio_service_.EnableWakeWordDetection(audio_service_.IsAfeWakeWord());
            }
            ESP_LOGI(TAG, "About to call audio_service_.ResetDecoder()");
            audio_service_.ResetDecoder();
            ESP_LOGI(TAG, "audio_service_.ResetDecoder() completed");
            ESP_LOGI(TAG, "kDeviceStateSpeaking state setup completed");
            break;
        }
        case kDeviceStateStarting:
        case kDeviceStateWifiConfiguring:
        case kDeviceStateUpgrading:
        case kDeviceStateActivating:
        case kDeviceStateAudioTesting:
        case kDeviceStateFatalError:
            // 未使用的状态在此不做处理
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    // Disconnect the audio channel
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        protocol_->CloseAudioChannel();
    }
    protocol_.reset();
    audio_service_.Stop();

    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
}

bool Application::UpgradeFirmware(Ota& ota, const std::string& url) {
    auto& board = Board::GetInstance();
    auto display = board.GetDisplay();
    
    // Use provided URL or get from OTA object
    std::string upgrade_url = url.empty() ? ota.GetFirmwareUrl() : url;
    std::string version_info = url.empty() ? ota.GetFirmwareVersion() : "(Manual upgrade)";
    
    // Close audio channel if it's open
    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Closing audio channel before firmware upgrade");
        protocol_->CloseAudioChannel();
    }
    ESP_LOGI(TAG, "Starting firmware upgrade from URL: %s", upgrade_url.c_str());
    
    Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "download", Lang::Sounds::OGG_UPGRADE);
    vTaskDelay(pdMS_TO_TICKS(3000));

    SetDeviceState(kDeviceStateUpgrading);
    
    std::string message = std::string(Lang::Strings::NEW_VERSION) + version_info;
    display->SetChatMessage("system", message.c_str());

    board.SetPowerSaveMode(false);
    audio_service_.Stop();
    vTaskDelay(pdMS_TO_TICKS(1000));

    bool upgrade_success = ota.StartUpgradeFromUrl(upgrade_url, [display](int progress, size_t speed) {
        std::thread([display, progress, speed]() {
            char buffer[32];
            snprintf(buffer, sizeof(buffer), "%d%% %uKB/s", progress, speed / 1024);
            display->SetChatMessage("system", buffer);
        }).detach();
    });

    if (!upgrade_success) {
        // Upgrade failed, restart audio service and continue running
        ESP_LOGE(TAG, "Firmware upgrade failed, restarting audio service and continuing operation...");
        audio_service_.Start(); // Restart audio service
        board.SetPowerSaveMode(true); // Restore power save mode
        Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "circle_xmark", Lang::Sounds::OGG_EXCLAMATION);
        vTaskDelay(pdMS_TO_TICKS(3000));
        return false;
    } else {
        // Upgrade success, reboot immediately
        ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
        display->SetChatMessage("system", "Upgrade successful, rebooting...");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
        Reboot();
        return true;
    }
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    // 如果设备处于空闲状态，则切换到聊天状态
    if (!protocol_) {
        return;
    }

    if (device_state_ == kDeviceStateIdle) {
        audio_service_.EncodeWakeWord();

        if (!protocol_->IsAudioChannelOpened()) {
            SetDeviceState(kDeviceStateConnecting);
            if (!protocol_->OpenAudioChannel()) {
                audio_service_.EnableWakeWordDetection(true);
                return;
            }
        }

        ESP_LOGI(TAG, "Wake word detected: %s", wake_word.c_str());
#if CONFIG_USE_AFE_WAKE_WORD || CONFIG_USE_CUSTOM_WAKE_WORD
        // Encode and send the wake word data to the server
        while (auto packet = audio_service_.PopWakeWordPacket()) {
            protocol_->SendAudio(std::move(packet));
        }
        // Set the chat state to wake word detected
        protocol_->SendWakeWordDetected(wake_word);
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
#else
        SetListeningMode(aec_mode_ == kAecOff ? kListeningModeAutoStop : kListeningModeRealtime);
        // Play the pop up sound to indicate the wake word is detected
        audio_service_.PlaySound(Lang::Sounds::OGG_POPUP);
#endif
    } else if (device_state_ == kDeviceStateSpeaking) {
        Schedule([this]() {
            AbortSpeaking(kAbortReasonNone);
        });
    } 
    // 如果设备处于监听状态，则关闭音频通道
    else if (device_state_ == kDeviceStateListening) {   
        Schedule([this]() {
            if (protocol_) {
                protocol_->CloseAudioChannel();
            }
        });
    }
}

void Application::HandleVoiceCommand(const std::string& command) {
    // 检查是否是风扇控制命令
    if (command.find("风扇") != std::string::npos) {
        ESP_LOGI(TAG, "Fan voice command detected: %s", command.c_str());
        // 通过board获取风扇控制器实例
        auto fan_controller = Board::GetInstance().GetFanController();
        if (fan_controller) {
            fan_controller->HandleVoiceCommand(command);
        } else {
            ESP_LOGW(TAG, "Fan controller not available");
        }
        return;
    }
    
    // 其他语音命令处理逻辑
    ESP_LOGI(TAG, "Processing voice command: %s", command.c_str());
}
/**
 * 触摸事件事件接口 - 只负责事件记录和调度
 * @param message 触摸事件消息
 * 
 * 作用：触摸传感器层调用此接口发送触摸事件
 * 此函数只做事件记录，不处理业务逻辑，确保架构分层清晰
 */
void Application::PostTouchEvent(const std::string& message) {
    ESP_LOGI(TAG, "Touch event posted: %s", message.c_str());
    // 去抖与合并：200ms 窗口合并最后一次触摸
    debounced_touch_message_ = message;
    last_touch_post_time_ms_ = esp_timer_get_time() / 1000;
    if (touch_debounce_timer_) {
        esp_timer_stop(touch_debounce_timer_);
        esp_timer_start_once(touch_debounce_timer_, 200000); // 200ms
    }
}

/**
 * 触摸事件处理函数 - 在主循环中执行所有业务逻辑
 * @param message 触摸事件消息
 * 
 * 作用：在主循环中统一处理触摸事件，确保状态一致性和操作顺序
 * 所有状态管理和业务逻辑都在主循环中执行，避免竞争条件
 */
void Application::ProcessTouchEvent(const std::string& message) {
    ESP_LOGI(TAG, "Processing touch event: %s", message.c_str());
    
    // 检查是否是风扇控制事件
    if (message.find("fan_button") != std::string::npos) {
        ESP_LOGI(TAG, "Fan button event detected: %s", message.c_str());
        // 风扇按键事件由FanController自动处理，这里只记录日志
        // 如果需要，可以通过Board::GetInstance().GetFanController()获取风扇控制器
        return;
    }
    
    // 1. 检查当前设备状态，决定处理策略
    ESP_LOGI(TAG, "Current device state: %s", STATE_STRINGS[device_state_]);
    
    // 2. 根据当前状态执行相应的触摸事件处理逻辑
    switch (device_state_) {
        case kDeviceStateIdle:
            // 空闲状态：直接处理触摸事件
            ESP_LOGI(TAG, "Device idle, processing touch event directly");
            HandleTouchEventInIdleState(message);
            break;
            
        case kDeviceStateSpeaking:
            // 说话状态：中止当前语音，然后处理触摸事件
            ESP_LOGI(TAG, "Device speaking, aborting speech for touch event");
            AbortSpeaking(kAbortReasonNone);
            // 等待中止完成后再处理触摸事件（非阻塞延迟 ~150ms）
            abort_delay_message_ = message;
            if (abort_delay_timer_) {
                esp_timer_stop(abort_delay_timer_);
                esp_timer_start_once(abort_delay_timer_, 150000);
            }
            break;
            
        case kDeviceStateListening:
            // 监听状态：停止监听，然后处理触摸事件
            ESP_LOGI(TAG, "Device listening, stopping listening for touch event");
            // 同闭包执行，避免跨 tick 抖动
            Schedule([this, message]() {
                protocol_->SendStopListening();
                SetDeviceState(kDeviceStateIdle);
                HandleTouchEventInIdleState(message);
            });
            break;
            
        case kDeviceStateConnecting:
            // 连接状态：等待连接完成后再处理触摸事件
            ESP_LOGI(TAG, "Device connecting, waiting for connection completion");
            // 退避重试：50,100,200,400,800ms，最多5次
            pending_touch_message_ = message;
            touch_retry_attempt_ = std::min(touch_retry_attempt_ + 1, 5);
            if (touch_retry_timer_) {
                uint64_t delay_ms = 50u << (touch_retry_attempt_ - 1);
                if (delay_ms > 800) delay_ms = 800;
                esp_timer_stop(touch_retry_timer_);
                esp_timer_start_once(touch_retry_timer_, delay_ms * 1000);
                ESP_LOGI(TAG, "Scheduled touch retry in %llu ms (attempt %d)", (unsigned long long)delay_ms, touch_retry_attempt_);
            }
            break;
            
        default:
            // 其他状态：等待回到空闲状态后再处理
            ESP_LOGW(TAG, "Device in state %s, waiting for idle state", STATE_STRINGS[device_state_]);
            // 统一复用重试逻辑
            pending_touch_message_ = message;
            touch_retry_attempt_ = std::min(touch_retry_attempt_ + 1, 5);
            if (touch_retry_timer_) {
                uint64_t delay_ms = 50u << (touch_retry_attempt_ - 1);
                if (delay_ms > 800) delay_ms = 800;
                esp_timer_stop(touch_retry_timer_);
                esp_timer_start_once(touch_retry_timer_, delay_ms * 1000);
            }
            break;
    }
}

/**
 * 在空闲状态下处理触摸事件
 * @param message 触摸事件消息
 * 
 * 作用：当设备处于空闲状态时，执行触摸事件的完整处理流程
 * 包括音频通道管理、消息发送、状态转换等
 */
void Application::HandleTouchEventInIdleState(const std::string& message) {
    ESP_LOGI(TAG, "Handling touch event in idle state: %s", message.c_str());
    
    // 1. 确保音频通道打开
    if (!protocol_ || !protocol_->IsAudioChannelOpened()) {
        ESP_LOGI(TAG, "Opening audio channel for touch event");
        SetDeviceState(kDeviceStateConnecting);
        
        if (!protocol_->OpenAudioChannel()) {
            ESP_LOGE(TAG, "Failed to open audio channel for touch event");
            // 连接失败，回到空闲状态
            SetDeviceState(kDeviceStateIdle);
            return;
        }
        // 等待音频通道打开完成
        ESP_LOGI(TAG, "Audio channel opened successfully");
    }
    
    // 2. 发送触摸事件消息
    // 注意：去重逻辑已在 OnTouchDebounce() 中处理，这里不再重复去重
    uint64_t now_ms = esp_timer_get_time() / 1000;
    if (protocol_) {
        ESP_LOGI(TAG, "Sending touch event message: %s", message.c_str());
        protocol_->SendMessage(message);
        ESP_LOGI(TAG, "Touch event message sent successfully");
        
        // 更新触摸事件时间戳，用于超时判断
        touch_event_time_ms_ = now_ms;
    } else {
        ESP_LOGE(TAG, "Protocol not available for touch event");
        return;
    }

    // 3. 检查是否启用保护模式（连续超时后跳过listen+start）
    bool in_protection = (direct_speaking_protection_mode_ && 
                          now_ms < protection_mode_until_ms_);
    
    if (in_protection) {
        // 保护模式：直接进入speaking保护，不等待tts start
        ESP_LOGW(TAG, "Direct speaking protection mode: skipping listen+start");
        SetDeviceState(kDeviceStateSpeaking);
        audio_service_.RefreshLastOutputTime();
        auto codec = Board::GetInstance().GetAudioCodec();
        if (codec) {
            codec->EnableOutput(true);
        }
        return;
    }
    
    // 4. 正常流程：不立即进入 listening，等待服务器 tts start（去监听化）
    // 启动触摸超时定时器（3秒），超时回退到 listening
    // touch_event_time_ms_ 已在发送消息或去重时更新
    if (touch_timeout_timer_) {
        esp_timer_stop(touch_timeout_timer_);
        esp_timer_start_once(touch_timeout_timer_, 3000000);
        ESP_LOGI(TAG, "Touch timeout timer started (3s)");
    }

    // 5. 确保音频通道仍然打开（可能在状态切换过程中被关闭）
    if (!protocol_ || !protocol_->IsAudioChannelOpened()) {
        ESP_LOGW(TAG, "Audio channel closed unexpectedly during touch event processing");
        SetDeviceState(kDeviceStateConnecting);
        if (!protocol_->OpenAudioChannel()) {
            ESP_LOGE(TAG, "Failed to reopen audio channel for touch event");
            SetDeviceState(kDeviceStateIdle);
            return;
        }
    }
    
    // 6. 确保音频输出启用（为首包做准备）
    auto codec = Board::GetInstance().GetAudioCodec();
    if (codec) {
        ESP_LOGI(TAG, "Ensuring audio output is enabled for touch event");
        codec->EnableOutput(true);
    }
    
    ESP_LOGI(TAG, "Touch event processing completed successfully");
}

bool Application::CanEnterSleepMode() {
    if (device_state_ != kDeviceStateIdle) {
        return false;
    }

    if (protocol_ && protocol_->IsAudioChannelOpened()) {
        return false;
    }

    if (!audio_service_.IsIdle()) {
        return false;
    }

    // Now it is safe to enter sleep mode
    return true;
}

void Application::SendMcpMessage(const std::string& payload) {
    if (protocol_ == nullptr) {
        return;
    }

    // Make sure you are using main thread to send MCP message
    if (xTaskGetCurrentTaskHandle() == main_event_loop_task_handle_) {
        protocol_->SendMcpMessage(payload);
    } else {
        Schedule([this, payload = std::move(payload)]() {
            protocol_->SendMcpMessage(payload);
        });
    }
}

void Application::SetAecMode(AecMode mode) {
    aec_mode_ = mode;
    Schedule([this]() {
        auto& board = Board::GetInstance();
        auto display = board.GetDisplay();
        switch (aec_mode_) {
        case kAecOff:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_OFF);
            break;
        case kAecOnServerSide:
            audio_service_.EnableDeviceAec(false);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        case kAecOnDeviceSide:
            audio_service_.EnableDeviceAec(true);
            display->ShowNotification(Lang::Strings::RTC_MODE_ON);
            break;
        }

        // If the AEC mode is changed, close the audio channel
        if (protocol_ && protocol_->IsAudioChannelOpened()) {
            protocol_->CloseAudioChannel();
        }
    });
}

void Application::PlaySound(const std::string_view& sound) {
    audio_service_.PlaySound(sound);
}