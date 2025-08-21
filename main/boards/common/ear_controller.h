#ifndef _EAR_CONTROLLER_H_
#define _EAR_CONTROLLER_H_

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"

#ifdef __cplusplus
extern "C" {
#endif

// Ear Control Structure
typedef struct {
    gpio_num_t ina_pin;
    gpio_num_t inb_pin;
    bool is_left_ear;
    ear_direction_t current_direction;
    ear_speed_t current_speed;
    bool is_active;
} ear_control_t;

// Ear Movement Pattern Structure
typedef struct {
    ear_direction_t direction;
    ear_speed_t speed;
    uint32_t duration_ms;
    uint32_t delay_ms;
} ear_movement_step_t;

// Ear Scenario Configuration
typedef struct {
    ear_scenario_t scenario;
    ear_movement_step_t *steps;
    uint8_t step_count;
    bool loop_enabled;
    uint8_t loop_count;
} ear_scenario_config_t;

// Function Declarations
esp_err_t ear_controller_init(void);
esp_err_t ear_controller_deinit(void);

// Basic Control Functions
esp_err_t ear_set_direction(bool left_ear, ear_direction_t direction);
esp_err_t ear_set_speed(bool left_ear, ear_speed_t speed);
esp_err_t ear_stop(bool left_ear);
esp_err_t ear_stop_both(void);

// Advanced Movement Functions
esp_err_t ear_move_timed(bool left_ear, ear_direction_t direction, 
                        ear_speed_t speed, uint32_t duration_ms);
esp_err_t ear_move_both_timed(ear_direction_t direction, 
                             ear_speed_t speed, uint32_t duration_ms);

// Scenario-based Functions
esp_err_t ear_play_scenario(ear_scenario_t scenario);
esp_err_t ear_play_scenario_async(ear_scenario_t scenario);
esp_err_t ear_stop_scenario(void);

// Specific Scenario Functions
esp_err_t ear_peekaboo_mode(uint32_t duration_ms);  // 躲猫猫模式
esp_err_t ear_insect_bite_mode(bool left_ear, uint32_t duration_ms);  // 蚊虫叮咬模式
esp_err_t ear_curious_mode(uint32_t duration_ms);   // 好奇模式
esp_err_t ear_sleepy_mode(void);                    // 困倦模式
esp_err_t ear_excited_mode(uint32_t duration_ms);   // 兴奋模式
esp_err_t ear_sad_mode(void);                       // 伤心模式
esp_err_t ear_alert_mode(void);                     // 警觉模式
esp_err_t ear_playful_mode(uint32_t duration_ms);   // 玩耍模式

// Custom Pattern Functions
esp_err_t ear_play_custom_pattern(ear_movement_step_t *steps, 
                                 uint8_t step_count, bool loop);
esp_err_t ear_set_custom_scenario(ear_scenario_config_t *config);

// Utility Functions
ear_direction_t ear_get_current_direction(bool left_ear);
ear_speed_t ear_get_current_speed(bool left_ear);
bool ear_is_moving(bool left_ear);
bool ear_is_scenario_active(void);

#ifdef __cplusplus
}
#endif

#endif // _EAR_CONTROLLER_H_
