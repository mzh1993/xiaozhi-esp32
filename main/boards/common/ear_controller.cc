#include "ear_controller.h"
#include "config.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>

static const char *TAG = "EAR_CONTROLLER";

// Global variables
static ear_control_t left_ear = {0};
static ear_control_t right_ear = {0};
static bool scenario_active = false;
static TimerHandle_t scenario_timer = NULL;
static ear_scenario_config_t current_scenario = {0};
static uint8_t current_step_index = 0;
static uint8_t current_loop_count = 0;

// Private function declarations
static void ear_set_gpio_levels(ear_control_t *ear, ear_direction_t direction);
static void ear_scenario_timer_callback(TimerHandle_t timer);
static uint32_t ear_speed_to_delay(ear_speed_t speed);
static void ear_apply_speed_control(ear_control_t *ear, ear_speed_t speed);

// Initialize ear controller
esp_err_t ear_controller_init(void) {
    ESP_LOGI(TAG, "Initializing ear controller");
    
    // Initialize left ear
    left_ear.ina_pin = LEFT_EAR_INA_GPIO;
    left_ear.inb_pin = LEFT_EAR_INB_GPIO;
    left_ear.is_left_ear = true;
    left_ear.current_direction = EAR_STOP;
    left_ear.current_speed = EAR_SPEED_NORMAL;
    left_ear.is_active = false;
    
    // Initialize right ear
    right_ear.ina_pin = RIGHT_EAR_INA_GPIO;
    right_ear.inb_pin = RIGHT_EAR_INB_GPIO;
    right_ear.is_left_ear = false;
    right_ear.current_direction = EAR_STOP;
    right_ear.current_speed = EAR_SPEED_NORMAL;
    right_ear.is_active = false;
    
    // Configure GPIO pins
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << LEFT_EAR_INA_GPIO) | (1ULL << LEFT_EAR_INB_GPIO) |
                       (1ULL << RIGHT_EAR_INA_GPIO) | (1ULL << RIGHT_EAR_INB_GPIO),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
    };
    
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO pins");
        return ret;
    }
    
    // Initialize all pins to low (stop state)
    gpio_set_level(LEFT_EAR_INA_GPIO, 0);
    gpio_set_level(LEFT_EAR_INB_GPIO, 0);
    gpio_set_level(RIGHT_EAR_INA_GPIO, 0);
    gpio_set_level(RIGHT_EAR_INB_GPIO, 0);
    
    // Create scenario timer
    scenario_timer = xTimerCreate("ear_scenario_timer", 
                                 pdMS_TO_TICKS(100), 
                                 pdTRUE, 
                                 NULL, 
                                 ear_scenario_timer_callback);
    
    if (scenario_timer == NULL) {
        ESP_LOGE(TAG, "Failed to create scenario timer");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Ear controller initialized successfully");
    return ESP_OK;
}

// Deinitialize ear controller
esp_err_t ear_controller_deinit(void) {
    ESP_LOGI(TAG, "Deinitializing ear controller");
    
    // Stop all ears
    ear_stop_both();
    
    // Delete timer
    if (scenario_timer != NULL) {
        xTimerDelete(scenario_timer, portMAX_DELAY);
        scenario_timer = NULL;
    }
    
    ESP_LOGI(TAG, "Ear controller deinitialized");
    return ESP_OK;
}

// Set ear direction with correct TC118S logic
static void ear_set_gpio_levels(ear_control_t *ear, ear_direction_t direction) {
    switch (direction) {
        case EAR_STOP:
            gpio_set_level(ear->ina_pin, 0);
            gpio_set_level(ear->inb_pin, 0);
            break;
        case EAR_FORWARD:
            gpio_set_level(ear->ina_pin, 1);
            gpio_set_level(ear->inb_pin, 0);
            break;
        case EAR_BACKWARD:
            gpio_set_level(ear->ina_pin, 0);
            gpio_set_level(ear->inb_pin, 1);
            break;
        case EAR_BRAKE:
            gpio_set_level(ear->ina_pin, 1);
            gpio_set_level(ear->inb_pin, 1);
            break;
    }
    ear->current_direction = direction;
}

// Convert speed to PWM delay for speed control
static uint32_t ear_speed_to_delay(ear_speed_t speed) {
    switch (speed) {
        case EAR_SPEED_SLOW: return 50;      // 50ms delay
        case EAR_SPEED_NORMAL: return 20;    // 20ms delay
        case EAR_SPEED_FAST: return 10;      // 10ms delay
        case EAR_SPEED_VERY_FAST: return 5;  // 5ms delay
        default: return 20;
    }
}

// Apply speed control using PWM-like technique
static void ear_apply_speed_control(ear_control_t *ear, ear_speed_t speed) {
    ear->current_speed = speed;
    // Speed control is implemented through timing in movement functions
}

// Set ear direction
esp_err_t ear_set_direction(bool left_ear, ear_direction_t direction) {
    ear_control_t *ear = left_ear ? &left_ear : &right_ear;
    ear_set_gpio_levels(ear, direction);
    ear->is_active = (direction != EAR_STOP);
    
    ESP_LOGI(TAG, "%s ear direction set to %d", 
             left_ear ? "Left" : "Right", direction);
    return ESP_OK;
}

// Set ear speed
esp_err_t ear_set_speed(bool left_ear, ear_speed_t speed) {
    ear_control_t *ear = left_ear ? &left_ear : &right_ear;
    ear_apply_speed_control(ear, speed);
    
    ESP_LOGI(TAG, "%s ear speed set to %d", 
             left_ear ? "Left" : "Right", speed);
    return ESP_OK;
}

// Stop single ear
esp_err_t ear_stop(bool left_ear) {
    return ear_set_direction(left_ear, EAR_STOP);
}

// Stop both ears
esp_err_t ear_stop_both(void) {
    ear_set_direction(true, EAR_STOP);
    ear_set_direction(false, EAR_STOP);
    return ESP_OK;
}

// Move ear with timing and speed control
esp_err_t ear_move_timed(bool left_ear, ear_direction_t direction, 
                        ear_speed_t speed, uint32_t duration_ms) {
    ear_control_t *ear = left_ear ? &left_ear : &right_ear;
    
    // Set direction and speed
    ear_set_direction(left_ear, direction);
    ear_set_speed(left_ear, speed);
    
    // Create a timer to stop the ear after duration
    if (duration_ms > 0) {
        TimerHandle_t stop_timer = xTimerCreate("ear_stop_timer",
                                              pdMS_TO_TICKS(duration_ms),
                                              pdFALSE,
                                              (void*)left_ear,
                                              [](TimerHandle_t timer) {
            bool is_left = (bool)pvTimerGetTimerID(timer);
            ear_stop(is_left);
            xTimerDelete(timer, 0);
        });
        
        if (stop_timer != NULL) {
            xTimerStart(stop_timer, 0);
        }
    }
    
    ESP_LOGI(TAG, "%s ear moving %d at speed %d for %d ms", 
             left_ear ? "Left" : "Right", direction, speed, duration_ms);
    return ESP_OK;
}

// Move both ears with timing
esp_err_t ear_move_both_timed(ear_direction_t direction, 
                             ear_speed_t speed, uint32_t duration_ms) {
    ear_move_timed(true, direction, speed, duration_ms);
    ear_move_timed(false, direction, speed, duration_ms);
    return ESP_OK;
}

// Scenario timer callback
static void ear_scenario_timer_callback(TimerHandle_t timer) {
    if (!scenario_active || current_scenario.steps == NULL) {
        return;
    }
    
    // Execute current step
    ear_movement_step_t *step = &current_scenario.steps[current_step_index];
    
    // Apply movement to both ears
    ear_move_timed(true, step->direction, step->speed, step->duration_ms);
    ear_move_timed(false, step->direction, step->speed, step->duration_ms);
    
    // Move to next step
    current_step_index++;
    
    // Check if scenario is complete
    if (current_step_index >= current_scenario.step_count) {
        current_step_index = 0;
        current_loop_count++;
        
        // Check if loops are complete
        if (!current_scenario.loop_enabled || 
            current_loop_count >= current_scenario.loop_count) {
            scenario_active = false;
            ear_stop_both();
            ESP_LOGI(TAG, "Scenario completed");
        }
    }
}

// Play scenario
esp_err_t ear_play_scenario(ear_scenario_t scenario) {
    ESP_LOGI(TAG, "Playing scenario: %d", scenario);
    
    // Stop any current scenario
    ear_stop_scenario();
    
    // Define scenario patterns
    static ear_movement_step_t peekaboo_steps[] = {
        {EAR_FORWARD, EAR_SPEED_NORMAL, 5000, 0}  // 5秒向前
    };
    
    static ear_movement_step_t insect_bite_steps[] = {
        {EAR_BACKWARD, EAR_SPEED_VERY_FAST, 200, 100},
        {EAR_FORWARD, EAR_SPEED_VERY_FAST, 200, 100},
        {EAR_BACKWARD, EAR_SPEED_VERY_FAST, 200, 100},
        {EAR_FORWARD, EAR_SPEED_VERY_FAST, 200, 100}
    };
    
    static ear_movement_step_t curious_steps[] = {
        {EAR_FORWARD, EAR_SPEED_NORMAL, 1000, 500},
        {EAR_BACKWARD, EAR_SPEED_NORMAL, 1000, 500}
    };
    
    static ear_movement_step_t excited_steps[] = {
        {EAR_FORWARD, EAR_SPEED_FAST, 300, 200},
        {EAR_BACKWARD, EAR_SPEED_FAST, 300, 200}
    };
    
    static ear_movement_step_t playful_steps[] = {
        {EAR_FORWARD, EAR_SPEED_NORMAL, 800, 300},
        {EAR_BACKWARD, EAR_SPEED_FAST, 400, 200},
        {EAR_FORWARD, EAR_SPEED_VERY_FAST, 200, 100},
        {EAR_BACKWARD, EAR_SPEED_NORMAL, 600, 400}
    };
    
    // Configure scenario based on type
    switch (scenario) {
        case EAR_SCENARIO_PEEKABOO:
            current_scenario.steps = peekaboo_steps;
            current_scenario.step_count = 1;
            current_scenario.loop_enabled = false;
            break;
            
        case EAR_SCENARIO_INSECT_BITE:
            current_scenario.steps = insect_bite_steps;
            current_scenario.step_count = 4;
            current_scenario.loop_enabled = true;
            current_scenario.loop_count = 5;  // 重复5次
            break;
            
        case EAR_SCENARIO_CURIOUS:
            current_scenario.steps = curious_steps;
            current_scenario.step_count = 2;
            current_scenario.loop_enabled = true;
            current_scenario.loop_count = 3;
            break;
            
        case EAR_SCENARIO_EXCITED:
            current_scenario.steps = excited_steps;
            current_scenario.step_count = 2;
            current_scenario.loop_enabled = true;
            current_scenario.loop_count = 8;
            break;
            
        case EAR_SCENARIO_PLAYFUL:
            current_scenario.steps = playful_steps;
            current_scenario.step_count = 4;
            current_scenario.loop_enabled = true;
            current_scenario.loop_count = 4;
            break;
            
        default:
            ESP_LOGW(TAG, "Unknown scenario: %d", scenario);
            return ESP_ERR_INVALID_ARG;
    }
    
    // Start scenario
    current_step_index = 0;
    current_loop_count = 0;
    scenario_active = true;
    
    // Start timer
    xTimerStart(scenario_timer, 0);
    
    return ESP_OK;
}

// Play scenario asynchronously
esp_err_t ear_play_scenario_async(ear_scenario_t scenario) {
    return ear_play_scenario(scenario);  // Already async
}

// Stop current scenario
esp_err_t ear_stop_scenario(void) {
    if (scenario_active) {
        scenario_active = false;
        xTimerStop(scenario_timer, 0);
        ear_stop_both();
        ESP_LOGI(TAG, "Scenario stopped");
    }
    return ESP_OK;
}

// Specific scenario functions
esp_err_t ear_peekaboo_mode(uint32_t duration_ms) {
    ear_stop_scenario();
    ear_move_both_timed(EAR_FORWARD, EAR_SPEED_NORMAL, duration_ms);
    return ESP_OK;
}

esp_err_t ear_insect_bite_mode(bool left_ear, uint32_t duration_ms) {
    ear_stop_scenario();
    
    // Create rapid back-and-forth movement
    for (int i = 0; i < 10; i++) {
        ear_move_timed(left_ear, EAR_BACKWARD, EAR_SPEED_VERY_FAST, 150);
        vTaskDelay(pdMS_TO_TICKS(100));
        ear_move_timed(left_ear, EAR_FORWARD, EAR_SPEED_VERY_FAST, 150);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return ESP_OK;
}

esp_err_t ear_curious_mode(uint32_t duration_ms) {
    ear_stop_scenario();
    
    // Alternate ear movement
    for (int i = 0; i < 3; i++) {
        ear_move_timed(true, EAR_FORWARD, EAR_SPEED_NORMAL, 1000);
        ear_move_timed(false, EAR_BACKWARD, EAR_SPEED_NORMAL, 1000);
        vTaskDelay(pdMS_TO_TICKS(500));
        ear_move_timed(true, EAR_BACKWARD, EAR_SPEED_NORMAL, 1000);
        ear_move_timed(false, EAR_FORWARD, EAR_SPEED_NORMAL, 1000);
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    
    return ESP_OK;
}

esp_err_t ear_sleepy_mode(void) {
    ear_stop_scenario();
    
    // Slow downward movement
    ear_move_both_timed(EAR_BACKWARD, EAR_SPEED_SLOW, 3000);
    return ESP_OK;
}

esp_err_t ear_excited_mode(uint32_t duration_ms) {
    ear_stop_scenario();
    
    // Fast alternating movement
    for (int i = 0; i < 10; i++) {
        ear_move_both_timed(EAR_FORWARD, EAR_SPEED_FAST, 200);
        vTaskDelay(pdMS_TO_TICKS(100));
        ear_move_both_timed(EAR_BACKWARD, EAR_SPEED_FAST, 200);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    return ESP_OK;
}

esp_err_t ear_sad_mode(void) {
    ear_stop_scenario();
    
    // Droop ears slowly
    ear_move_both_timed(EAR_BACKWARD, EAR_SPEED_SLOW, 2000);
    return ESP_OK;
}

esp_err_t ear_alert_mode(void) {
    ear_stop_scenario();
    
    // Perk up ears quickly
    ear_move_both_timed(EAR_FORWARD, EAR_SPEED_FAST, 500);
    return ESP_OK;
}

esp_err_t ear_playful_mode(uint32_t duration_ms) {
    ear_stop_scenario();
    
    // Random-like playful movement
    for (int i = 0; i < 8; i++) {
        ear_move_timed(true, EAR_FORWARD, EAR_SPEED_NORMAL, 400);
        ear_move_timed(false, EAR_BACKWARD, EAR_SPEED_FAST, 300);
        vTaskDelay(pdMS_TO_TICKS(200));
        ear_move_timed(true, EAR_BACKWARD, EAR_SPEED_FAST, 200);
        ear_move_timed(false, EAR_FORWARD, EAR_SPEED_NORMAL, 500);
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    
    return ESP_OK;
}

// Utility functions
ear_direction_t ear_get_current_direction(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear : &right_ear;
    return ear->current_direction;
}

ear_speed_t ear_get_current_speed(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear : &right_ear;
    return ear->current_speed;
}

bool ear_is_moving(bool left_ear) {
    ear_control_t *ear = left_ear ? &left_ear : &right_ear;
    return ear->is_active;
}

bool ear_is_scenario_active(void) {
    return scenario_active;
}
