#include "application.h"
#include "board.h"
#include "display.h"
#include "system_info.h"
#include "audio_codec.h"
#include "mqtt_protocol.h"
#include "websocket_protocol.h"
#include "font_awesome_symbols.h"
#include "assets/lang_config.h"
#include "mcp_server.h"

#include <cstring>
#include <esp_log.h>
#include <cJSON.h>
#include <driver/gpio.h>
#include <arpa/inet.h>

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
            app->OnClockTimer();
        },
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "clock_timer",
        .skip_unhandled_events = true
    };
    esp_timer_create(&clock_timer_args, &clock_timer_handle_);
}

Application::~Application() {
    if (clock_timer_handle_ != nullptr) {
        esp_timer_stop(clock_timer_handle_);
        esp_timer_delete(clock_timer_handle_);
    }
    vEventGroupDelete(event_group_);
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

            char buffer[128];
            snprintf(buffer, sizeof(buffer), Lang::Strings::CHECK_NEW_VERSION_FAILED, retry_delay, ota.GetCheckVersionUrl().c_str());
            Alert(Lang::Strings::ERROR, buffer, "sad", Lang::Sounds::P3_EXCLAMATION);

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
            Alert(Lang::Strings::OTA_UPGRADE, Lang::Strings::UPGRADING, "happy", Lang::Sounds::P3_UPGRADE);

            vTaskDelay(pdMS_TO_TICKS(3000));

            SetDeviceState(kDeviceStateUpgrading);
            
            display->SetIcon(FONT_AWESOME_DOWNLOAD);
            std::string message = std::string(Lang::Strings::NEW_VERSION) + ota.GetFirmwareVersion();
            display->SetChatMessage("system", message.c_str());

            board.SetPowerSaveMode(false);
            audio_service_.Stop();
            vTaskDelay(pdMS_TO_TICKS(1000));

            bool upgrade_success = ota.StartUpgrade([display](int progress, size_t speed) {
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
                Alert(Lang::Strings::ERROR, Lang::Strings::UPGRADE_FAILED, "sad", Lang::Sounds::P3_EXCLAMATION);
                vTaskDelay(pdMS_TO_TICKS(3000));
                // Continue to normal operation (don't break, just fall through)
            } else {
                // Upgrade success, reboot immediately
                ESP_LOGI(TAG, "Firmware upgrade successful, rebooting...");
                display->SetChatMessage("system", "Upgrade successful, rebooting...");
                vTaskDelay(pdMS_TO_TICKS(1000)); // Brief pause to show message
                Reboot();
                return; // This line will never be reached after reboot
            }
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
        digit_sound{'0', Lang::Sounds::P3_0},
        digit_sound{'1', Lang::Sounds::P3_1}, 
        digit_sound{'2', Lang::Sounds::P3_2},
        digit_sound{'3', Lang::Sounds::P3_3},
        digit_sound{'4', Lang::Sounds::P3_4},
        digit_sound{'5', Lang::Sounds::P3_5},
        digit_sound{'6', Lang::Sounds::P3_6},
        digit_sound{'7', Lang::Sounds::P3_7},
        digit_sound{'8', Lang::Sounds::P3_8},
        digit_sound{'9', Lang::Sounds::P3_9}
    }};

    // This sentence uses 9KB of SRAM, so we need to wait for it to finish
    Alert(Lang::Strings::ACTIVATION, message.c_str(), "happy", Lang::Sounds::P3_ACTIVATION);

    for (const auto& digit : code) {
        auto it = std::find_if(digit_sounds.begin(), digit_sounds.end(),
            [digit](const digit_sound& ds) { return ds.digit == digit; });
        if (it != digit_sounds.end()) {
            audio_service_.PlaySound(it->sound);
        }
    }
}

void Application::Alert(const char* status, const char* message, const char* emotion, const std::string_view& sound) {
    ESP_LOGW(TAG, "Alert %s: %s [%s]", status, message, emotion);
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

    /* Setup the audio service */
    auto codec = board.GetAudioCodec();
    audio_service_.Initialize(codec);
    audio_service_.Start();

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

    /* Start the clock timer to update the status bar */
    esp_timer_start_periodic(clock_timer_handle_, 1000000);

    /* Wait for the network to be ready */
    board.StartNetwork();

    // Update the status bar immediately to show the network state
    display->UpdateStatusBar(true);

    // Check for new firmware version or get the MQTT broker address
    Ota ota;
    CheckNewVersion(ota);

    // Initialize the protocol
    display->SetStatus(Lang::Strings::LOADING_PROTOCOL);

    // Add MCP common tools before initializing the protocol
    McpServer::GetInstance().AddCommonTools();

    if (ota.HasMqttConfig()) {
        protocol_ = std::make_unique<MqttProtocol>();
    } else if (ota.HasWebsocketConfig()) {
        protocol_ = std::make_unique<WebsocketProtocol>();
    } else {
        ESP_LOGW(TAG, "No protocol specified in the OTA config, using MQTT");
        protocol_ = std::make_unique<MqttProtocol>();
    }

    protocol_->OnNetworkError([this](const std::string& message) {
        last_error_message_ = message;
        xEventGroupSetBits(event_group_, MAIN_EVENT_ERROR);
    });
    protocol_->OnIncomingAudio([this](std::unique_ptr<AudioStreamPacket> packet) {
        if (device_state_ == kDeviceStateSpeaking) {
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
                    if (device_state_ == kDeviceStateIdle || device_state_ == kDeviceStateListening) {
                        SetDeviceState(kDeviceStateSpeaking);
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
                ESP_LOGI(TAG, "Received LLM emotion: %s", emotion->valuestring);
                Schedule([this, display, emotion_str = std::string(emotion->valuestring)]() {
                    ESP_LOGI(TAG, "Processing LLM emotion in main loop: %s", emotion_str.c_str());
                    
                    // 检查情绪是否发生变化
                    static std::string last_emotion = "";
                    if (last_emotion != emotion_str) {
                        ESP_LOGI(TAG, "Emotion changed from '%s' to '%s'", last_emotion.c_str(), emotion_str.c_str());
                        last_emotion = emotion_str;
                        
                        // 更新显示
                        display->SetEmotion(emotion_str.c_str());
                        
                        // 触发对应的耳朵动作
                        auto ear_controller = Board::GetInstance().GetEarController();
                        ESP_LOGI(TAG, "Got ear controller: %s", ear_controller ? "valid" : "null");
                        if (ear_controller) {
                            ESP_LOGI(TAG, "Triggering ear emotion: %s", emotion_str.c_str());
                            esp_err_t ret = ear_controller->TriggerByEmotion(emotion_str.c_str());
                            ESP_LOGI(TAG, "Ear emotion trigger result: %s", (ret == ESP_OK) ? "success" : "failed");
                        } else {
                            ESP_LOGW(TAG, "No ear controller available for emotion: %s", emotion_str.c_str());
                        }
                    } else {
                        ESP_LOGI(TAG, "Emotion unchanged: %s, skipping ear action", emotion_str.c_str());
                        // 只更新显示，不触发耳朵动作
                        display->SetEmotion(emotion_str.c_str());
                    }
                });
            } else {
                ESP_LOGW(TAG, "LLM message missing or invalid emotion field");
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
                Alert(status->valuestring, message->valuestring, emotion->valuestring, Lang::Sounds::P3_VIBRATION);
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

    SetDeviceState(kDeviceStateIdle);

    has_server_time_ = ota.HasServerTime();
    if (protocol_started) {
        std::string message = std::string(Lang::Strings::VERSION) + ota.GetCurrentVersion();
        display->ShowNotification(message.c_str());
        display->SetChatMessage("system", "");
        // Play the success sound to indicate the device is ready
        audio_service_.PlaySound(Lang::Sounds::P3_SUCCESS);
    }

    // Initialize ear controller emotion mappings
    auto ear_controller = Board::GetInstance().GetEarController();
    ESP_LOGI(TAG, "Getting ear controller for emotion mapping initialization: %s", ear_controller ? "valid" : "null");
    if (ear_controller) {
        ESP_LOGI(TAG, "Starting ear controller emotion mapping initialization");
        // 设置默认情绪映射
        esp_err_t ret;
        
        ret = ear_controller->SetEmotionMapping("neutral", EAR_SCENARIO_NORMAL, 0);
        ESP_LOGI(TAG, "Set neutral mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("happy", EAR_SCENARIO_PLAYFUL, 3000);
        ESP_LOGI(TAG, "Set happy mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("laughing", EAR_SCENARIO_EXCITED, 4000);
        ESP_LOGI(TAG, "Set laughing mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("funny", EAR_SCENARIO_PLAYFUL, 2500);
        ESP_LOGI(TAG, "Set funny mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("sad", EAR_SCENARIO_SAD, 0);
        ESP_LOGI(TAG, "Set sad mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("angry", EAR_SCENARIO_ALERT, 2000);
        ESP_LOGI(TAG, "Set angry mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("crying", EAR_SCENARIO_SAD, 0);
        ESP_LOGI(TAG, "Set crying mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("loving", EAR_SCENARIO_CURIOUS, 2000);
        ESP_LOGI(TAG, "Set loving mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("embarrassed", EAR_SCENARIO_SAD, 1500);
        ESP_LOGI(TAG, "Set embarrassed mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("surprised", EAR_SCENARIO_ALERT, 1000);
        ESP_LOGI(TAG, "Set surprised mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("shocked", EAR_SCENARIO_ALERT, 1500);
        ESP_LOGI(TAG, "Set shocked mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("thinking", EAR_SCENARIO_CURIOUS, 3000);
        ESP_LOGI(TAG, "Set thinking mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("winking", EAR_SCENARIO_PLAYFUL, 1500);
        ESP_LOGI(TAG, "Set winking mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("cool", EAR_SCENARIO_ALERT, 1000);
        ESP_LOGI(TAG, "Set cool mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("relaxed", EAR_SCENARIO_NORMAL, 0);
        ESP_LOGI(TAG, "Set relaxed mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("delicious", EAR_SCENARIO_EXCITED, 2000);
        ESP_LOGI(TAG, "Set delicious mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("kissy", EAR_SCENARIO_CURIOUS, 1500);
        ESP_LOGI(TAG, "Set kissy mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("confident", EAR_SCENARIO_ALERT, 1000);
        ESP_LOGI(TAG, "Set confident mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("sleepy", EAR_SCENARIO_SLEEPY, 0);
        ESP_LOGI(TAG, "Set sleepy mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("silly", EAR_SCENARIO_PLAYFUL, 3000);
        ESP_LOGI(TAG, "Set silly mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ret = ear_controller->SetEmotionMapping("confused", EAR_SCENARIO_CURIOUS, 2500);
        ESP_LOGI(TAG, "Set confused mapping: %s", (ret == ESP_OK) ? "success" : "failed");
        
        ESP_LOGI(TAG, "Ear controller emotion mapping initialization completed");
    } else {
        ESP_LOGW(TAG, "No ear controller available for emotion mapping initialization");
    }
    
    // Print heap stats
    SystemInfo::PrintHeapStats();
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

// The Main Event Loop controls the chat state and websocket connection
// If other tasks need to access the websocket or chat state,
// they should use Schedule to call this function
void Application::MainEventLoop() {
    // Raise the priority of the main event loop to avoid being interrupted by background tasks (which has priority 2)
    vTaskPrioritySet(NULL, 3);

    while (true) {
        auto bits = xEventGroupWaitBits(event_group_, MAIN_EVENT_SCHEDULE |
            MAIN_EVENT_SEND_AUDIO |
            MAIN_EVENT_WAKE_WORD_DETECTED |
            MAIN_EVENT_VAD_CHANGE |
            MAIN_EVENT_ERROR, pdTRUE, pdFALSE, portMAX_DELAY);
        if (bits & MAIN_EVENT_ERROR) {
            SetDeviceState(kDeviceStateIdle);
            Alert(Lang::Strings::ERROR, last_error_message_.c_str(), "sad", Lang::Sounds::P3_EXCLAMATION);
        }

        if (bits & MAIN_EVENT_SEND_AUDIO) {
            while (auto packet = audio_service_.PopPacketFromSendQueue()) {
                if (!protocol_->SendAudio(std::move(packet))) {
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
        audio_service_.PlaySound(Lang::Sounds::P3_POPUP);
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
    protocol_->SendAbortSpeaking(reason);
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
                ESP_LOGI(TAG, "Device entering idle state, ensuring ears are down");
                ear_controller->EnsureEarsDown();
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
            ESP_LOGI(TAG, "Entering kDeviceStateSpeaking state");
            display->SetStatus(Lang::Strings::SPEAKING);

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
            }
            ESP_LOGI(TAG, "About to call audio_service_.ResetDecoder()");
            audio_service_.ResetDecoder();
            ESP_LOGI(TAG, "audio_service_.ResetDecoder() completed");
            ESP_LOGI(TAG, "kDeviceStateSpeaking state setup completed");
            break;
        default:
            // Do nothing
            break;
    }
}

void Application::Reboot() {
    ESP_LOGI(TAG, "Rebooting...");
    esp_restart();
}

void Application::WakeWordInvoke(const std::string& wake_word) {
    // 如果设备处于空闲状态，则切换到聊天状态
    if (device_state_ == kDeviceStateIdle) {
        ToggleChatState();
        Schedule([this, wake_word]() {
            if (protocol_) {
                protocol_->SendWakeWordDetected(wake_word); 
            }
        }); 
    }
    // 如果设备处于说话状态，则停止说话
    else if (device_state_ == kDeviceStateSpeaking) {
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
/**
 * 触摸事件事件接口 - 只负责事件记录和调度
 * @param message 触摸事件消息
 * 
 * 作用：触摸传感器层调用此接口发送触摸事件
 * 此函数只做事件记录，不处理业务逻辑，确保架构分层清晰
 */
void Application::PostTouchEvent(const std::string& message) {
    ESP_LOGI(TAG, "Touch event posted: %s", message.c_str());
    
    // 通过Schedule放入主循环，确保状态一致性
    Schedule([this, message]() {
        ProcessTouchEvent(message);
    });
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
            // 等待中止完成后再处理触摸事件
            Schedule([this, message]() {
                HandleTouchEventInIdleState(message);
            });
            break;
            
        case kDeviceStateListening:
            // 监听状态：停止监听，然后处理触摸事件
            ESP_LOGI(TAG, "Device listening, stopping listening for touch event");
            protocol_->SendStopListening();
            SetDeviceState(kDeviceStateIdle);
            // 等待状态转换完成后再处理触摸事件
            Schedule([this, message]() {
                HandleTouchEventInIdleState(message);
            });
            break;
            
        case kDeviceStateConnecting:
            // 连接状态：等待连接完成后再处理触摸事件
            ESP_LOGI(TAG, "Device connecting, waiting for connection completion");
            Schedule([this, message]() {
                ProcessTouchEvent(message);
            });
            break;
            
        default:
            // 其他状态：等待回到空闲状态后再处理
            ESP_LOGW(TAG, "Device in state %s, waiting for idle state", STATE_STRINGS[device_state_]);
            Schedule([this, message]() {
                ProcessTouchEvent(message);
            });
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
    if (protocol_) {
        ESP_LOGI(TAG, "Sending touch event message: %s", message.c_str());
        protocol_->SendMessage(message);
        ESP_LOGI(TAG, "Touch event message sent successfully");
    } else {
        ESP_LOGE(TAG, "Protocol not available for touch event");
        return;
    }

    // 3. 先切换到监听状态以启用音频处理（参考唤醒词流程）
    ESP_LOGI(TAG, "Switching to Listening state to enable audio processing");
    ESP_LOGI(TAG, "About to call SetListeningMode(kListeningModeAutoStop)");
    SetListeningMode(kListeningModeAutoStop);
    ESP_LOGI(TAG, "SetListeningMode(kListeningModeAutoStop) completed");
    
    // 4. 延迟切换到说话状态，给音频处理足够时间建立
    ESP_LOGI(TAG, "Delaying switch to Speaking state to allow audio processing to establish");
    // 使用任务延迟给音频处理足够时间建立
    vTaskDelay(pdMS_TO_TICKS(500)); // 延迟1秒，给音频处理足够时间
    ESP_LOGI(TAG, "Delayed switch to Speaking state for touch event response");
    SetDeviceState(kDeviceStateSpeaking);
    
    // 5. 确保音频输出启用（解决音频输出被禁用的问题）
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
    Schedule([this, payload]() {
        if (protocol_) {
            protocol_->SendMcpMessage(payload);
        }
    });
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