#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

static const char *TAG = "MAIN_APP";

// ============================================================================
// 1. HARDWARE PINS & ADC CONFIGURATION
// ============================================================================
#define BUTTON_GPIO             GPIO_NUM_9          // Onboard BOOT button
#define BUTTON_DEBOUNCE_MS      50                  // Resonant bounce filter time
#define BUTTON_HOLD_TIME_MS     5000                // 5-second long-press threshold

#define BATTERY_ADC_CHANNEL     ADC_CHANNEL_1       // GPIO2 (ADC1 CH1)
#define SERVO_FB_ADC_CHANNEL    ADC_CHANNEL_3       // GPIO4 (ADC1 CH3)

#define BATTERY_ADC_ATTEN       ADC_ATTEN_DB_12     // 0-3.3V range
#define SERVO_FB_ADC_ATTEN      ADC_ATTEN_DB_12     // 0-3.3V range

#define VOLTAGE_DIVIDER_RATIO   5.7f
#define OVERSAMPLE_COUNT        32

static adc_oneshot_unit_handle_t adc1_handle;
static adc_cali_handle_t cali_handle_bat = NULL;
static adc_cali_handle_t cali_handle_fb = NULL;

static void adc_init(void)
{
    adc_oneshot_unit_init_cfg_t init_config1 = { .unit_id = ADC_UNIT_1 };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config1, &adc1_handle));

    adc_oneshot_chan_cfg_t config_bat = {
        .atten = BATTERY_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, BATTERY_ADC_CHANNEL, &config_bat));

    adc_oneshot_chan_cfg_t config_fb = {
        .atten = SERVO_FB_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc1_handle, SERVO_FB_ADC_CHANNEL, &config_fb));

#if CONFIG_IDF_TARGET_ESP32H2
    adc_cali_curve_fitting_config_t cali_config_bat = {
        .unit_id = ADC_UNIT_1, .chan = BATTERY_ADC_CHANNEL, .atten = BATTERY_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    adc_cali_create_scheme_curve_fitting(&cali_config_bat, &cali_handle_bat);

    adc_cali_curve_fitting_config_t cali_config_fb = {
        .unit_id = ADC_UNIT_1, .chan = SERVO_FB_ADC_CHANNEL, .atten = SERVO_FB_ADC_ATTEN, .bitwidth = ADC_BITWIDTH_DEFAULT
    };
    adc_cali_create_scheme_curve_fitting(&cali_config_fb, &cali_handle_fb);
#endif
}

static float read_battery_voltage(void)
{
    int raw_val = 0, pin_voltage_mv = 0;
    ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, BATTERY_ADC_CHANNEL, &raw_val));
    if (cali_handle_bat) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle_bat, raw_val, &pin_voltage_mv));
    } else {
        pin_voltage_mv = (raw_val * 3300) / 4095;
    }
    return (pin_voltage_mv * VOLTAGE_DIVIDER_RATIO) / 1000.0f;
}

static void read_servo_feedback(int *raw_out, int *mv_out)
{
    vTaskDelay(pdMS_TO_TICKS(100)); // Supply settling delay

    uint32_t raw_sum = 0;
    int single_raw = 0;
    for (int i = 0; i < OVERSAMPLE_COUNT; i++) {
        ESP_ERROR_CHECK(adc_oneshot_read(adc1_handle, SERVO_FB_ADC_CHANNEL, &single_raw));
        raw_sum += single_raw;
        esp_rom_delay_us(100);
    }
    int avg_raw = raw_sum / OVERSAMPLE_COUNT;
    int voltage_mv = 0;

    if (cali_handle_fb) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(cali_handle_fb, avg_raw, &voltage_mv));
    } else {
        voltage_mv = (avg_raw * 3300) / 4095;
    }
    *raw_out = avg_raw;
    *mv_out = voltage_mv;
}

// ============================================================================
// 2. SERVO PWM CONFIGURATION & MOVEMENT HELPERS
// ============================================================================
#define SERVO_GPIO              GPIO_NUM_3
#define SERVO_LEDC_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define SERVO_LEDC_CHANNEL      LEDC_CHANNEL_0
#define SERVO_LEDC_TIMER        LEDC_TIMER_0
#define SERVO_LEDC_DUTY_RES     LEDC_TIMER_14_BIT
#define SERVO_LEDC_FREQ_HZ      50

#define SERVO_MIN_PULSE_WIDTH_US 500
#define SERVO_MAX_PULSE_WIDTH_US 2500
#define SERVO_MAX_DEGREE         270

static int Calibration_Sensitivity     = 50;  // Range: 1 (sensitive) to 100 (stall only)
static int Calibration_Step_Size       = 5;   // Degrees per step (1 to 45)
static int Calibration_Backoff_Degrees = 20;  // Safe margin backed off after stall confirmed (0 to 45)

static int calib_min_angle = 0;
static int calib_max_angle = SERVO_MAX_DEGREE;

static void servo_init(void)
{
    ledc_timer_config_t ledc_timer = {
        .speed_mode       = SERVO_LEDC_SPEED_MODE,
        .timer_num        = SERVO_LEDC_TIMER,
        .duty_resolution  = SERVO_LEDC_DUTY_RES,
        .freq_hz          = SERVO_LEDC_FREQ_HZ,
        .clk_cfg          = LEDC_AUTO_CLK
    };
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));

    ledc_channel_config_t ledc_channel = {
        .speed_mode     = SERVO_LEDC_SPEED_MODE,
        .channel        = SERVO_LEDC_CHANNEL,
        .timer_sel      = SERVO_LEDC_TIMER,
        .intr_type      = LEDC_INTR_DISABLE,
        .gpio_num       = SERVO_GPIO,
        .duty           = 0,
        .hpoint         = 0
    };
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));
}

static void set_servo_angle(int angle)
{
    if (angle < calib_min_angle) angle = calib_min_angle;
    if (angle > calib_max_angle) angle = calib_max_angle;

    uint32_t pulse_width_us = SERVO_MIN_PULSE_WIDTH_US + 
        (((uint32_t)angle * (SERVO_MAX_PULSE_WIDTH_US - SERVO_MIN_PULSE_WIDTH_US)) / SERVO_MAX_DEGREE);

    uint32_t max_duty = (1 << 14) - 1;
    uint32_t duty = (pulse_width_us * max_duty) / 20000;

    ESP_ERROR_CHECK(ledc_set_duty(SERVO_LEDC_SPEED_MODE, SERVO_LEDC_CHANNEL, duty));
    ESP_ERROR_CHECK(ledc_update_duty(SERVO_LEDC_SPEED_MODE, SERVO_LEDC_CHANNEL));
}

static void move_servo_degrees(int target_angle)
{
    int clamped_angle = target_angle;
    if (clamped_angle < calib_min_angle) clamped_angle = calib_min_angle;
    if (clamped_angle > calib_max_angle) clamped_angle = calib_max_angle;

    set_servo_angle(target_angle);
    vTaskDelay(pdMS_TO_TICKS(600));

    int raw_fb, mv_fb;
    read_servo_feedback(&raw_fb, &mv_fb);

    if (target_angle != clamped_angle) {
        printf("Warning: Target %d deg clamped to safe limit %d deg.\n", target_angle, clamped_angle);
    }
    printf("Servo turned to %d deg | Feedback: %d mV (ADC Raw: %d)\n", clamped_angle, mv_fb, raw_fb);
}

static void move_servo_percentage(int pct)
{
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;

    float span = (float)(calib_max_angle - calib_min_angle);
    int clamped_angle = (int)(calib_min_angle + roundf((pct / 100.0f) * span));

    set_servo_angle(clamped_angle);
    vTaskDelay(pdMS_TO_TICKS(600));

    int raw_fb, mv_fb;
    read_servo_feedback(&raw_fb, &mv_fb);

    printf("Servo turned to %d%% (%d deg) [Limits: %d..%d deg] | Feedback: %d mV (ADC Raw: %d)\n",
           pct, clamped_angle, calib_min_angle, calib_max_angle, mv_fb, raw_fb);
}

static int calculate_stop_threshold(int ref_delta, int sensitivity)
{
    if (sensitivity >= 100) return 1;

    float factor = 0.80f - ((float)(sensitivity - 1) * (0.75f / 98.0f));
    int threshold = (int)(ref_delta * factor);

    return (threshold > 1) ? threshold : 1;
}

// ============================================================================
// 3. STEP-BASED CALIBRATION & BACKOFF ROUTINE
// ============================================================================
static void calibrate_servo(void)
{
    printf("\n--- Starting Servo Calibration ---\n");
    printf("Config: Sensitivity = %d/100 | Step Size = %d deg | Backoff Margin = %d deg\n", 
           Calibration_Sensitivity, Calibration_Step_Size, Calibration_Backoff_Degrees);

    calib_min_angle = 0;
    calib_max_angle = SERVO_MAX_DEGREE;

    int raw_fb, mv_fb, prev_mv;

    // --- SCAN MAX LIMIT ---
    printf("\n1. Moving to center (135 deg)...\n");
    set_servo_angle(135);
    vTaskDelay(pdMS_TO_TICKS(1000));
    read_servo_feedback(&raw_fb, &mv_fb);
    prev_mv = mv_fb;

    printf("2. Sampling 5 baseline steps (+%d deg) on GPIO4...\n", Calibration_Step_Size);
    int baseline_sum_progress = 0;
    int curr_ang = 135;

    for (int i = 0; i < 5; i++) {
        int next_ang = curr_ang + Calibration_Step_Size;
        if (next_ang > SERVO_MAX_DEGREE) break;

        set_servo_angle(next_ang);
        vTaskDelay(pdMS_TO_TICKS(350));
        read_servo_feedback(&raw_fb, &mv_fb);

        baseline_sum_progress += (mv_fb - prev_mv);
        printf("  [Baseline %d/5 @ %3d deg] FB: %4d mV | Step Change: %+d mV\n", i + 1, next_ang, mv_fb, mv_fb - prev_mv);
        prev_mv = mv_fb;
        curr_ang = next_ang;
    }

    int dir_max = (baseline_sum_progress >= 0) ? 1 : -1;
    int ref_delta = abs(baseline_sum_progress) / 5;
    if (ref_delta < 2) ref_delta = 2;
    int thresh = calculate_stop_threshold(ref_delta, Calibration_Sensitivity);

    printf("  -> Polarity: %s | Avg Step: %d mV | Stop Thresh: <= %d mV\n",
           (dir_max > 0) ? "Positive (+mV)" : "Negative (-mV)", ref_delta, thresh);
    printf("3. Scanning for MAX limit...\n");

    for (int ang = curr_ang + Calibration_Step_Size; ang <= SERVO_MAX_DEGREE; ang += Calibration_Step_Size) {
        set_servo_angle(ang);
        vTaskDelay(pdMS_TO_TICKS(350));
        read_servo_feedback(&raw_fb, &mv_fb);

        int progress = dir_max * (mv_fb - prev_mv);
        printf("  [Angle %3d] FB: %4d mV | Forward Progress: %+2d mV\n", ang, mv_fb, progress);

        if (progress <= thresh) {
            int pre_stall_ang = ang - Calibration_Step_Size;
            if (pre_stall_ang < 0) pre_stall_ang = 0;

            printf("  -> Candidate stall detected at %d deg (Progress %+d <= Thresh %d).\n", ang, progress, thresh);
            printf("  -> Verifying stall at %d deg by stepping back %d deg to %d deg (3 retries)...\n", 
                   ang, Calibration_Step_Size, pre_stall_ang);

            bool is_real_stall = true;

            for (int retry = 1; retry <= 3; retry++) {
                set_servo_angle(pre_stall_ang);
                vTaskDelay(pdMS_TO_TICKS(350));
                read_servo_feedback(&raw_fb, &mv_fb);
                int pre_mv = mv_fb;

                set_servo_angle(ang);
                vTaskDelay(pdMS_TO_TICKS(350));
                read_servo_feedback(&raw_fb, &mv_fb);

                int retry_progress = dir_max * (mv_fb - pre_mv);
                printf("     [Retry %d/3 @ %3d deg] Progress from %d deg: %+2d mV\n", retry, ang, pre_stall_ang, retry_progress);

                if (retry_progress > thresh) {
                    printf("     -> Motion restored! False alarm cleared.\n");
                    is_real_stall = false;
                    prev_mv = mv_fb;
                    break;
                }
            }

            if (is_real_stall) {
                int final_limit = ang - Calibration_Backoff_Degrees;
                if (final_limit < 0) final_limit = 0;

                printf("  -> Hard limit confirmed at %d deg! Backing off %d deg to safe limit: %d deg.\n", 
                       ang, Calibration_Backoff_Degrees, final_limit);
                calib_max_angle = final_limit;
                set_servo_angle(final_limit);
                break;
            }
        } else {
            prev_mv = mv_fb;
        }

        if (ang + Calibration_Step_Size > SERVO_MAX_DEGREE) {
            calib_max_angle = SERVO_MAX_DEGREE;
        }
    }

    // --- SCAN MIN LIMIT ---
    printf("\n4. Returning to center (135 deg)...\n");
    set_servo_angle(135);
    vTaskDelay(pdMS_TO_TICKS(1000));
    read_servo_feedback(&raw_fb, &mv_fb);
    prev_mv = mv_fb;

    printf("5. Sampling 5 baseline steps (-%d deg) on GPIO4...\n", Calibration_Step_Size);
    baseline_sum_progress = 0;
    curr_ang = 135;

    for (int i = 0; i < 5; i++) {
        int next_ang = curr_ang - Calibration_Step_Size;
        if (next_ang < 0) break;

        set_servo_angle(next_ang);
        vTaskDelay(pdMS_TO_TICKS(350));
        read_servo_feedback(&raw_fb, &mv_fb);

        baseline_sum_progress += (mv_fb - prev_mv);
        printf("  [Baseline %d/5 @ %3d deg] FB: %4d mV | Step Change: %+d mV\n", i + 1, next_ang, mv_fb, mv_fb - prev_mv);
        prev_mv = mv_fb;
        curr_ang = next_ang;
    }

    int dir_min = (baseline_sum_progress >= 0) ? 1 : -1;
    ref_delta = abs(baseline_sum_progress) / 5;
    if (ref_delta < 2) ref_delta = 2;
    thresh = calculate_stop_threshold(ref_delta, Calibration_Sensitivity);

    printf("  -> Polarity: %s | Avg Step: %d mV | Stop Thresh: <= %d mV\n",
           (dir_min > 0) ? "Positive (+mV)" : "Negative (-mV)", ref_delta, thresh);
    printf("6. Scanning for MIN limit...\n");

    for (int ang = curr_ang - Calibration_Step_Size; ang >= 0; ang -= Calibration_Step_Size) {
        set_servo_angle(ang);
        vTaskDelay(pdMS_TO_TICKS(350));
        read_servo_feedback(&raw_fb, &mv_fb);

        int progress = dir_min * (mv_fb - prev_mv);
        printf("  [Angle %3d] FB: %4d mV | Forward Progress: %+2d mV\n", ang, mv_fb, progress);

        if (progress <= thresh) {
            int pre_stall_ang = ang + Calibration_Step_Size;
            if (pre_stall_ang > SERVO_MAX_DEGREE) pre_stall_ang = SERVO_MAX_DEGREE;

            printf("  -> Candidate stall detected at %d deg (Progress %+d <= Thresh %d).\n", ang, progress, thresh);
            printf("  -> Verifying stall at %d deg by stepping back %d deg to %d deg (3 retries)...\n", 
                   ang, Calibration_Step_Size, pre_stall_ang);

            bool is_real_stall = true;

            for (int retry = 1; retry <= 3; retry++) {
                set_servo_angle(pre_stall_ang);
                vTaskDelay(pdMS_TO_TICKS(350));
                read_servo_feedback(&raw_fb, &mv_fb);
                int pre_mv = mv_fb;

                set_servo_angle(ang);
                vTaskDelay(pdMS_TO_TICKS(350));
                read_servo_feedback(&raw_fb, &mv_fb);

                int retry_progress = dir_min * (mv_fb - pre_mv);
                printf("     [Retry %d/3 @ %3d deg] Progress from %d deg: %+2d mV\n", retry, ang, pre_stall_ang, retry_progress);

                if (retry_progress > thresh) {
                    printf("     -> Motion restored! False alarm cleared.\n");
                    is_real_stall = false;
                    prev_mv = mv_fb;
                    break;
                }
            }

            if (is_real_stall) {
                int final_limit = ang + Calibration_Backoff_Degrees;
                if (final_limit > SERVO_MAX_DEGREE) final_limit = SERVO_MAX_DEGREE;

                printf("  -> Hard limit confirmed at %d deg! Backing off %d deg to safe limit: %d deg.\n", 
                       ang, Calibration_Backoff_Degrees, final_limit);
                calib_min_angle = final_limit;
                set_servo_angle(final_limit);
                break;
            }
        } else {
            prev_mv = mv_fb;
        }

        if (ang - Calibration_Step_Size < 0) {
            calib_min_angle = 0;
        }
    }

    set_servo_angle(135);
    printf("\n--- Calibration Complete ---\n");
    printf("Safe Operating Range: %d to %d degrees\n\n", calib_min_angle, calib_max_angle);
}

// ============================================================================
// 4. DEBOUNCED BUTTON TASK (SHORT PRESS CYCLE + LONG PRESS CALIBRATION)
// ============================================================================
static void button_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BUTTON_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);

    bool is_pressed = false;
    bool triggered = false;
    TickType_t press_start_time = 0;
    int last_printed_sec = 0;

    // Preset percentages for short press cycling (0% -> 50% -> 100% -> 50% -> 0%)
    static const int cycle_pcts[] = {0, 50, 100, 50};
    static const int num_cycle_steps = sizeof(cycle_pcts) / sizeof(cycle_pcts[0]);
    static int cycle_idx = 0;

    while (1) {
        // Active LOW: Pressed = 0, Released = 1
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            if (!is_pressed) {
                // Filter resonance bounce with 50ms verification delay
                vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
                if (gpio_get_level(BUTTON_GPIO) == 0) {
                    is_pressed = true;
                    triggered = false;
                    press_start_time = xTaskGetTickCount();
                    last_printed_sec = 0;
                }
            } else if (!triggered) {
                uint32_t elapsed_ms = pdTICKS_TO_MS(xTaskGetTickCount() - press_start_time);
                int elapsed_sec = elapsed_ms / 1000;

                if (elapsed_sec > last_printed_sec && elapsed_sec < 5) {
                    last_printed_sec = elapsed_sec;
                    printf("[Button] Holding... %d/5 sec (Hold 5s to calibrate)\n", elapsed_sec);
                }

                if (elapsed_ms >= BUTTON_HOLD_TIME_MS) {
                    triggered = true;
                    printf("\n[Button] 5-second long press confirmed! Triggering calibration...\n");
                    calibrate_servo();
                }
            }
        } else {
            if (is_pressed) {
                // Filter release bounce
                vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_MS));
                if (gpio_get_level(BUTTON_GPIO) == 1) {
                    if (!triggered) {
                        // Short Press Handler: Cycle 0% -> 50% -> 100% -> 50% -> 0%
                        int target_pct = cycle_pcts[cycle_idx];
                        cycle_idx = (cycle_idx + 1) % num_cycle_steps;

                        printf("\n[Button] Short press -> Moving to %d%%\n", target_pct);
                        move_servo_percentage(target_pct);
                    }
                    is_pressed = false;
                    triggered = false;
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================================
// 5. COMMAND PROCESSOR & MAIN LOOP
// ============================================================================
static void process_command(const char *cmd)
{
    if (strcasecmp(cmd, "Battery") == 0) {
        float volts = read_battery_voltage();
        printf("Battery Voltage: %.2f V\n", volts);
    }
    else if (strcasecmp(cmd, "Calibrate") == 0) {
        calibrate_servo();
    }
    else if (strncasecmp(cmd, "Sensitivity", 11) == 0) {
        const char *arg = cmd + 11;
        while (*arg == ' ') arg++;

        if (*arg != '\0') {
            int val = atoi(arg);
            if (val >= 1 && val <= 100) {
                Calibration_Sensitivity = val;
                printf("Calibration_Sensitivity set to %d\n", Calibration_Sensitivity);
            } else {
                printf("Error: Sensitivity must be between 1 and 100.\n");
            }
        } else {
            printf("Current Calibration_Sensitivity: %d\n", Calibration_Sensitivity);
        }
    }
    else if (strncasecmp(cmd, "StepSize", 8) == 0) {
        const char *arg = cmd + 8;
        while (*arg == ' ') arg++;

        if (*arg != '\0') {
            int val = atoi(arg);
            if (val >= 1 && val <= 45) {
                Calibration_Step_Size = val;
                printf("Calibration_Step_Size set to %d degrees\n", Calibration_Step_Size);
            } else {
                printf("Error: StepSize must be between 1 and 45 degrees.\n");
            }
        } else {
            printf("Current Calibration_Step_Size: %d degrees\n", Calibration_Step_Size);
        }
    }
    else if (strncasecmp(cmd, "Backoff", 7) == 0) {
        const char *arg = cmd + 7;
        while (*arg == ' ') arg++;

        if (*arg != '\0') {
            int val = atoi(arg);
            if (val >= 0 && val <= 45) {
                Calibration_Backoff_Degrees = val;
                printf("Calibration_Backoff_Degrees set to %d degrees\n", Calibration_Backoff_Degrees);
            } else {
                printf("Error: Backoff must be between 0 and 45 degrees.\n");
            }
        } else {
            printf("Current Calibration_Backoff_Degrees: %d degrees\n", Calibration_Backoff_Degrees);
        }
    }
    else if (strncasecmp(cmd, "Servo ", 6) == 0) {
        const char *arg = cmd + 6;
        while (*arg == ' ') arg++;

        bool is_percentage = (strchr(arg, '%') != NULL);
        int raw_val = atoi(arg);

        if (is_percentage) {
            move_servo_percentage(raw_val);
        } else {
            move_servo_degrees(raw_val);
        }
    }
    else {
        printf("Unknown command: '%s'. Available commands:\n", cmd);
        printf("  'Battery', 'Servo <nnn|nnn%%>', 'Calibrate'\n");
        printf("  'Sensitivity [1-100]', 'StepSize [1-45]', 'Backoff [0-45]'\n");
    }
}

void app_main(void)
{
    adc_init();
    servo_init();
    set_servo_angle(135);

    // Spawn debounced BOOT button monitor task
    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);

    printf("\n=== ESP32-H2 Interactive Controller Ready ===\n");
    printf("Commands:\n");
    printf("  1. 'Battery'             -> Returns battery voltage\n");
    printf("  2. 'Servo nnn' or 'nnn%%' -> Sets servo angle in deg or relative %%\n");
    printf("  3. 'Calibrate'           -> Runs auto calibration routine\n");
    printf("  4. 'Sensitivity [1-100]' -> Gets or sets calibration sensitivity\n");
    printf("  5. 'StepSize [1-45]'     -> Gets or sets calibration step size (deg)\n");
    printf("  6. 'Backoff [0-45]'      -> Gets or sets safe backoff margin (deg)\n");
    printf("  7. [BOOT Button]         -> Short press: cycles 0%% -> 50%% -> 100%% -> 50%% -> 0%%\n");
    printf("                              Long press (5s): runs calibration\n\n");

    static char line_buf[128];
    static size_t line_pos = 0;

    while (1) {
        int c = getchar();

        if (c != EOF) {
            putchar(c);
            fflush(stdout);

            if (c == '\r' || c == '\n') {
                printf("\n");
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    process_command(line_buf);
                    line_pos = 0;
                }
            }
            else if (c == '\b' || c == 127) {
                if (line_pos > 0) line_pos--;
            }
            else if (line_pos < sizeof(line_buf) - 1) {
                line_buf[line_pos++] = (char)c;
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
}