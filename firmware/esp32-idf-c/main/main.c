/* ============================================================
 * Smart Tourniquet System — Firmware (C murni, ESP-IDF native)
 * Kelompok 19 — Desain Proyek Teknik Elektro, Komputer, Biomedik 2
 *
 * File tunggal (bukan Arduino C++): semua logic ada di sini —
 * konversi sensor, filter Butterworth, PID, driver aktuator, UI,
 * dan state machine — ditulis dalam ANSI C, dikompilasi lewat
 * ESP-IDF (framework = espidf di platformio.ini, BUKAN framework
 * arduino, karena Arduino API seperti Serial/Wire adalah objek
 * C++ dan tidak bisa dipakai dari file .c murni).
 *
 * State machine sesuai proposal Bab V.B & VI.C:
 *   IDLE -> SELF_CHECK -> ARMED -> INFLATING -> SAMPLING ->
 *   LOP_DETECTED -> HOLDING -> MONITORING -> ALARM ->
 *   SLOW_RELEASE -> DEFLATED
 *
 * CATATAN JUJUR soal driver OLED:
 * Driver SSD1306 lengkap (rendering teks berbasis font bitmap)
 * itu ratusan baris kode tersendiri di luar scope file ini.
 * Fungsi ui_show_message() di bawah MENGIRIM data ke OLED lewat
 * I2C command dasar (init sequence + clear), tapi rendering teks
 * disederhanakan jadi ESP_LOGI (tampil di serial monitor) sebagai
 * placeholder. Untuk render teks asli ke layar, tim perlu
 * menambahkan font table + fungsi draw_char()/draw_string() —
 * atau memakai komponen esp-idf resmi (mis. "ssd1306" di ESP
 * Component Registry) yang sudah lengkap dengan font rendering.
 * ============================================================ */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"

static const char *TAG = "TOURNIQUET";

/* ============================================================
 * BAGIAN 1: KONFIGURASI PIN & PARAMETER
 * (sebelumnya config.h, sekarang jadi bagian atas main.c)
 * ============================================================ */

/* --- I2C (ADS1115 + OLED) --- */
#define I2C_PORT            I2C_NUM_0
#define I2C_SDA_PIN         21
#define I2C_SCL_PIN         22
#define I2C_FREQ_HZ         400000
#define ADS1115_ADDR        0x48
#define OLED_ADDR           0x3C

/* --- Aktuator --- */
#define PUMP_MOSFET_PIN         GPIO_NUM_25   /* PWM lewat LEDC ke gate MOSFET driver pompa */
#define SOLENOID_LOCK_PIN       GPIO_NUM_26   /* solenoid valve NC — kunci tekanan */
#define SOLENOID_RELEASE_PIN    GPIO_NUM_27   /* solenoid valve slow-release — T-Conversion */

/* --- UI --- */
#define BTN_START_PIN       GPIO_NUM_32
#define BTN_RELEASE_PIN     GPIO_NUM_33
#define BUZZER_PIN          GPIO_NUM_14
#define LED_GREEN_PIN       GPIO_NUM_12
#define LED_YELLOW_PIN      GPIO_NUM_13
#define LED_RED_PIN         GPIO_NUM_15

/* --- Kalibrasi Sensor MPX5050DP + divider tegangan (lihat Docs/perhitungan_sensor.md) ---
 * REVISI: ADS1115 disupply 3.3V (BUKAN 5V) untuk menghindari isu level tegangan I2C dengan
 * ESP32-S3. Vout sensor MPX5050DP (bisa sampai ~5V) diturunkan dulu pakai voltage divider
 * R1=2.2k / R2=3.3k (rasio 0.6) sebelum masuk ADS1115. Kompensasi rasio ini dilakukan di
 * pressure_read_mmhg() di bawah.
 */
#define DIVIDER_RATIO                     0.6f
#define MPX5050_SENSITIVITY_V_PER_MMHG    0.012f
#define MPX5050_OFFSET_V                  0.2f
#define MPX5050_RATED_RANGE_MMHG          375.0f  /* batas operasi terkalibrasi (POP spec datasheet) */

/* --- Parameter Klinis (PDS) ---
 * CATATAN TERBUKA: target hard limit 400 mmHg berada di luar rentang rated sensor (375 mmHg).
 * Ini KONFLIK REQUIREMENT yang belum diputuskan tim (lihat Docs/perhitungan_sensor.md bagian 4.1).
 * Nilai di bawah memakai 400 sesuai PDS asli -- ganti ke 375 kalau tim memutuskan Opsi A
 * (turunkan hard limit) sebagai solusi final.
 */
#define LOP_TARGET_MIN_UPPER_LIMB   150.0f
#define LOP_TARGET_MAX_UPPER_LIMB   250.0f
#define LOP_TARGET_MIN_LOWER_LIMB   200.0f
#define LOP_TARGET_MAX_LOWER_LIMB   320.0f
#define HARD_PRESSURE_LIMIT_MMHG    400.0f

/* --- DSP --- */
#define ADC_SAMPLE_RATE_HZ       50
#define SAMPLING_WINDOW_SEC      3
#define BUTTERWORTH_SECTIONS     5
#define PEAK_THRESHOLD_MIN_COUNT 3
#define PEAK_WINDOW_SAMPLES      25
/* Settling time pasca-reset filter, dihitung dari group delay Butterworth di 3Hz (~10.4 sampel)
 * x margin 2.5 (lihat Algorithms/butterworth_filter_design.c). Lihat juga catatan revisi di
 * Docs/perhitungan_butterworth.md bagian 5 -- ini BELUM final, masih perlu diuji dgn phantom arm. */
#define FILTER_SETTLE_SAMPLES    27
#define USE_CONTINUOUS_FILTER    0   /* 1 = filter tidak pernah direset antar window (Opsi A) */

/* --- Monitoring Aktif --- */
#define MONITOR_INTERVAL_SEC          15
#define PRESSURE_DROP_REINFLATE_MMHG  15.0f
#define PRESSURE_DROP_LEAK_MMHG       30.0f
#define USAGE_ALARM_INTERVAL_MIN      30

/* --- T-Conversion --- */
#define SLOW_RELEASE_RATE_MMHG_PER_MIN   20.0f

/* --- PWM pompa --- */
#define PUMP_LEDC_TIMER      LEDC_TIMER_0
#define PUMP_LEDC_MODE       LEDC_LOW_SPEED_MODE
#define PUMP_LEDC_CHANNEL    LEDC_CHANNEL_0
#define PUMP_PWM_FREQ_HZ     20000
#define PUMP_PWM_RES_BITS    LEDC_TIMER_8_BIT   /* duty 0-255 */

/* ============================================================
 * BAGIAN 2: I2C HELPER (dipakai ADS1115 & OLED)
 * ============================================================ */

static esp_err_t i2c_master_init(void) {
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_PORT, &conf);
    return i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
}

static esp_err_t i2c_write_bytes(uint8_t addr, const uint8_t *data, size_t len) {
    return i2c_master_write_to_device(I2C_PORT, addr, data, len, pdMS_TO_TICKS(100));
}

static esp_err_t i2c_read_bytes(uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
    return i2c_master_write_read_device(I2C_PORT, addr, &reg, 1, data, len, pdMS_TO_TICKS(100));
}

/* ============================================================
 * BAGIAN 3: DRIVER SENSOR TEKANAN (ADS1115 + MPX5050DP)
 * ============================================================ */

#define ADS1115_REG_CONVERSION  0x00
#define ADS1115_REG_CONFIG      0x01

/* Config register ADS1115 untuk single-shot, channel AIN0, gain GAIN_ONE (+-4.096V),
 * data rate 128SPS. GAIN_ONE dipakai karena Vadc maksimum setelah divider ~3.0V,
 * cukup di dalam rentang +-4.096V dengan resolusi lebih baik dari GAIN_TWOTHIRDS. */
static esp_err_t ads1115_start_conversion(uint8_t channel) {
    uint16_t mux = 0x4000 | (channel << 12); /* single-ended AINx vs GND */
    uint16_t config = 0x8000        /* OS: start single conversion */
                     | mux
                     | 0x0200        /* PGA: GAIN_ONE, +-4.096V */
                     | 0x0100        /* MODE: single-shot */
                     | 0x0080        /* DR: 128 SPS */
                     | 0x0003;       /* disable comparator */

    uint8_t buf[3];
    buf[0] = ADS1115_REG_CONFIG;
    buf[1] = (config >> 8) & 0xFF;
    buf[2] = config & 0xFF;
    return i2c_write_bytes(ADS1115_ADDR, buf, 3);
}

static int16_t ads1115_read_conversion(void) {
    uint8_t data[2] = {0};
    i2c_read_bytes(ADS1115_ADDR, ADS1115_REG_CONVERSION, data, 2);
    return (int16_t)((data[0] << 8) | data[1]);
}

static bool pressure_sensor_init(void) {
    /* ADS1115 tidak butuh init khusus selain konfigurasi per-pembacaan; cek device ada
     * dengan mencoba baca satu konversi. */
    esp_err_t err = ads1115_start_conversion(0);
    return (err == ESP_OK);
}

/* Baca tekanan dalam mmHg dari channel ADC tertentu (default channel 0) */
static float pressure_read_mmhg(uint8_t channel) {
    ads1115_start_conversion(channel);
    vTaskDelay(pdMS_TO_TICKS(8)); /* tunggu konversi selesai (~7.8ms @ 128SPS) */
    int16_t raw = ads1115_read_conversion();

    const float ADS1115_LSB_VOLT = 4.096f / 32768.0f; /* untuk GAIN_ONE */
    float vadc = raw * ADS1115_LSB_VOLT;

    /* kompensasi voltage divider -> Vout ASLI sensor sebelum dibagi */
    float vout_sensor = vadc / DIVIDER_RATIO;

    float pressure = (vout_sensor - MPX5050_OFFSET_V) / MPX5050_SENSITIVITY_V_PER_MMHG;
    return pressure;
}

/* ============================================================
 * BAGIAN 4: FILTER BUTTERWORTH BANDPASS ORDE 5 (1-5Hz @ 50Hz)
 * Koefisien dihitung dengan scipy (lihat Algorithms/butterworth_filter_design.c untuk
 * versi C yang menghitung ulang & memvalidasi angka yang sama).
 * ============================================================ */

typedef struct {
    float b0, b1, b2;
    float a1, a2;
    float z1, z2; /* state Direct Form II Transposed */
} biquad_section_t;

typedef struct {
    biquad_section_t sections[BUTTERWORTH_SECTIONS];
} butterworth_bpf_t;

static void biquad_reset(biquad_section_t *s) {
    s->z1 = 0.0f;
    s->z2 = 0.0f;
}

static float biquad_process(biquad_section_t *s, float x) {
    float y = s->b0 * x + s->z1;
    s->z1 = s->b1 * x - s->a1 * y + s->z2;
    s->z2 = s->b2 * x - s->a2 * y;
    return y;
}

static void butterworth_init(butterworth_bpf_t *f) {
    /* clang-format off */
    f->sections[0] = (biquad_section_t){0.0004894361f, 0.0009788722f, 0.0004894361f, -1.3198965304f, 0.5281847481f, 0, 0};
    f->sections[1] = (biquad_section_t){1.0000000000f, 2.0000000000f, 1.0000000000f, -1.5276383342f, 0.5913983514f, 0, 0};
    f->sections[2] = (biquad_section_t){1.0000000000f, 0.0000000000f, -1.0000000000f, -1.4527179777f, 0.7819157637f, 0, 0};
    f->sections[3] = (biquad_section_t){1.0000000000f, -2.0000000000f, 1.0000000000f, -1.8105486220f, 0.8313586394f, 0, 0};
    f->sections[4] = (biquad_section_t){1.0000000000f, -2.0000000000f, 1.0000000000f, -1.9315415226f, 0.9474689283f, 0, 0};
    /* clang-format on */
}

static float butterworth_process(butterworth_bpf_t *f, float x) {
    float y = x;
    for (int i = 0; i < BUTTERWORTH_SECTIONS; i++) {
        y = biquad_process(&f->sections[i], y);
    }
    return y;
}

static void butterworth_reset(butterworth_bpf_t *f) {
#if !USE_CONTINUOUS_FILTER
    for (int i = 0; i < BUTTERWORTH_SECTIONS; i++) {
        biquad_reset(&f->sections[i]);
    }
#endif
    /* kalau USE_CONTINUOUS_FILTER == 1, fungsi ini sengaja jadi no-op (Opsi A, lihat catatan
     * di Docs/perhitungan_butterworth.md bagian 5) */
}

/* ---------- Peak detector (sliding window 0.5s = 25 sampel) ---------- */

typedef struct {
    float buffer[PEAK_WINDOW_SAMPLES];
    int idx;
    int count;
    int peak_count;
    int settle_counter;
    float prominence_threshold; /* mmHg, NILAI AWAL -- wajib dikalibrasi dari data phantom arm */
} peak_detector_t;

static void peak_detector_reset(peak_detector_t *pd) {
    pd->idx = 0;
    pd->count = 0;
    pd->peak_count = 0;
    pd->settle_counter = 0;
    if (pd->prominence_threshold == 0.0f) {
        pd->prominence_threshold = 0.05f; /* default awal */
    }
}

static void peak_detector_add_sample(peak_detector_t *pd, float value) {
#if !USE_CONTINUOUS_FILTER
    if (pd->settle_counter < FILTER_SETTLE_SAMPLES) {
        pd->settle_counter++;
        return; /* buang sampel ini, filter belum settle pasca-reset */
    }
#endif

    pd->buffer[pd->idx % PEAK_WINDOW_SAMPLES] = value;
    pd->idx++;
    if (pd->count < PEAK_WINDOW_SAMPLES) pd->count++;

    if (pd->count == PEAK_WINDOW_SAMPLES) {
        int mid_idx = (pd->idx - 1 - PEAK_WINDOW_SAMPLES / 2) % PEAK_WINDOW_SAMPLES;
        if (mid_idx < 0) mid_idx += PEAK_WINDOW_SAMPLES;
        float mid_val = pd->buffer[mid_idx];
        bool is_peak = true;
        for (int k = 0; k < PEAK_WINDOW_SAMPLES; k++) {
            if (k == mid_idx) continue;
            if (pd->buffer[k] > mid_val) {
                is_peak = false;
                break;
            }
        }
        if (is_peak && mid_val > pd->prominence_threshold) {
            pd->peak_count++;
        }
    }
}

/* ============================================================
 * BAGIAN 5: PID CONTROLLER (kontrol duty PWM pompa)
 * ============================================================ */

typedef struct {
    float kp, ki, kd;
    float integral;
    float last_error;
    int64_t last_time_us;
} pid_controller_t;

static void pid_init(pid_controller_t *pid, float kp, float ki, float kd) {
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->integral = 0.0f;
    pid->last_error = 0.0f;
    pid->last_time_us = esp_timer_get_time();
}

static void pid_reset(pid_controller_t *pid) {
    pid->integral = 0.0f;
    pid->last_error = 0.0f;
    pid->last_time_us = esp_timer_get_time();
}

/* setpoint & measured dalam mmHg, output di-clamp ke [0,255] untuk duty PWM pompa */
static uint8_t pid_compute(pid_controller_t *pid, float setpoint, float measured) {
    int64_t now_us = esp_timer_get_time();
    float dt = (now_us - pid->last_time_us) / 1e6f;
    if (dt <= 0.0f) dt = 0.001f;

    float error = setpoint - measured;
    pid->integral += error * dt;

    const float integral_max = 100.0f; /* anti-windup */
    if (pid->integral > integral_max) pid->integral = integral_max;
    if (pid->integral < -integral_max) pid->integral = -integral_max;

    float derivative = (error - pid->last_error) / dt;
    float output = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;

    pid->last_error = error;
    pid->last_time_us = now_us;

    if (output < 0) output = 0;
    if (output > 255) output = 255;
    return (uint8_t)output;
}

/* ============================================================
 * BAGIAN 6: DRIVER AKTUATOR (pompa PWM + 2 solenoid ON/OFF)
 *
 * CATATAN REVISI pemilihan MOSFET (lihat Docs/perhitungan_driver.md bagian 1):
 * Rekomendasi IRLZ44N sebelumnya kurang tepat tanpa syarat -- RDS(on) yang dipakai di
 * perhitungan daya adalah nilai @VGS=5V, bukan kondisi terjamin di VGS=3.3V (GPIO ESP32-S3).
 * Fungsi di bawah bekerja sama terlepas dari MOSFET fisik yang dipasang -- part yang dipilih
 * harus dari salah satu opsi: (A) MOSFET dgn RDS(on) terjamin @2.5-3.3V, (B) IRLZ44N +
 * validasi empiris, atau (C) IRLZ44N/IRF540N + pre-driver IC (IR2104).
 * ============================================================ */

static void actuator_init(void) {
    ledc_timer_config_t timer_conf = {
        .speed_mode = PUMP_LEDC_MODE,
        .timer_num = PUMP_LEDC_TIMER,
        .duty_resolution = PUMP_PWM_RES_BITS,
        .freq_hz = PUMP_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&timer_conf);

    ledc_channel_config_t ch_conf = {
        .gpio_num = PUMP_MOSFET_PIN,
        .speed_mode = PUMP_LEDC_MODE,
        .channel = PUMP_LEDC_CHANNEL,
        .timer_sel = PUMP_LEDC_TIMER,
        .duty = 0,
        .hpoint = 0,
    };
    ledc_channel_config(&ch_conf);

    gpio_set_direction(SOLENOID_LOCK_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(SOLENOID_RELEASE_PIN, GPIO_MODE_OUTPUT);

    /* default state aman: pompa off, semua solenoid closed */
    ledc_set_duty(PUMP_LEDC_MODE, PUMP_LEDC_CHANNEL, 0);
    ledc_update_duty(PUMP_LEDC_MODE, PUMP_LEDC_CHANNEL);
    gpio_set_level(SOLENOID_LOCK_PIN, 0);
    gpio_set_level(SOLENOID_RELEASE_PIN, 0);
}

static void actuator_set_pump_duty(uint8_t duty) {
    ledc_set_duty(PUMP_LEDC_MODE, PUMP_LEDC_CHANNEL, duty);
    ledc_update_duty(PUMP_LEDC_MODE, PUMP_LEDC_CHANNEL);
}

static void actuator_pump_off(void) {
    actuator_set_pump_duty(0);
}

static void actuator_lock_pressure(bool locked) {
    gpio_set_level(SOLENOID_LOCK_PIN, locked ? 1 : 0);
}

static void actuator_release_slow(bool releasing) {
    gpio_set_level(SOLENOID_RELEASE_PIN, releasing ? 1 : 0);
}

static void actuator_emergency_deflate(void) {
    actuator_pump_off();
    actuator_lock_pressure(false);
    actuator_release_slow(true);
}

/* ============================================================
 * BAGIAN 7: DRIVER UI (buzzer, LED, OLED sederhana)
 *
 * CATATAN JUJUR: ini bukan driver SSD1306 lengkap (lihat catatan di kepala file).
 * showMessage() dan showPressureStatus() memakai ESP_LOGI sebagai placeholder tampilan;
 * untuk render teks asli ke layar OLED, tambahkan font table + fungsi gambar karakter,
 * atau pakai komponen esp-idf resmi untuk SSD1306.
 * ============================================================ */

static void ui_init(void) {
    gpio_set_direction(BUZZER_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_GREEN_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_YELLOW_PIN, GPIO_MODE_OUTPUT);
    gpio_set_direction(LED_RED_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(LED_GREEN_PIN, 0);
    gpio_set_level(LED_YELLOW_PIN, 0);
    gpio_set_level(LED_RED_PIN, 0);
    gpio_set_level(BUZZER_PIN, 0);

    /* Minimal init sequence SSD1306 lewat I2C (display ON, tanpa rendering teks) */
    uint8_t init_cmds[] = {0x00, 0xAE, 0xA4, 0xAF}; /* 0x00=cmd mode, display off, resume, display on */
    i2c_write_bytes(OLED_ADDR, init_cmds, sizeof(init_cmds));
}

static void ui_show_message(const char *line1, const char *line2) {
    ESP_LOGI(TAG, "[OLED] %s | %s", line1, line2 ? line2 : "");
}

static void ui_show_pressure_status(float pressure_mmhg, const char *state_name, unsigned long elapsed_sec) {
    ESP_LOGI(TAG, "[OLED] TEKANAN: %.0f mmHg | Status: %s | Waktu: %02lu:%02lu",
             pressure_mmhg, state_name, elapsed_sec / 60, elapsed_sec % 60);
}

static void ui_all_leds_off(void) {
    gpio_set_level(LED_GREEN_PIN, 0);
    gpio_set_level(LED_YELLOW_PIN, 0);
    gpio_set_level(LED_RED_PIN, 0);
}

static void ui_status_ok(void) {
    ui_all_leds_off();
    gpio_set_level(LED_GREEN_PIN, 1);
}

static void ui_status_critical(void) {
    ui_all_leds_off();
    gpio_set_level(LED_RED_PIN, 1);
}

static void ui_buzzer_beep_short(void) {
    gpio_set_level(BUZZER_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(150));
    gpio_set_level(BUZZER_PIN, 0);
}

static void ui_buzzer_on(void) {
    gpio_set_level(BUZZER_PIN, 1);
}

static void ui_buzzer_off(void) {
    gpio_set_level(BUZZER_PIN, 0);
}

/* ============================================================
 * BAGIAN 8: STATE MACHINE
 * ============================================================ */

typedef enum {
    STATE_IDLE,
    STATE_SELF_CHECK,
    STATE_ARMED,
    STATE_INFLATING,
    STATE_SAMPLING,
    STATE_LOP_DETECTED,
    STATE_HOLDING,
    STATE_MONITORING,
    STATE_ALARM,
    STATE_SLOW_RELEASE,
    STATE_DEFLATED
} tourniquet_state_t;

static const char *state_to_string(tourniquet_state_t s) {
    switch (s) {
        case STATE_IDLE:         return "IDLE";
        case STATE_SELF_CHECK:   return "SELF_CHECK";
        case STATE_ARMED:        return "ARMED";
        case STATE_INFLATING:    return "INFLATING";
        case STATE_SAMPLING:     return "SAMPLING";
        case STATE_LOP_DETECTED: return "LOP_DETECTED";
        case STATE_HOLDING:      return "HOLDING";
        case STATE_MONITORING:   return "MONITORING";
        case STATE_ALARM:        return "ALARM";
        case STATE_SLOW_RELEASE: return "SLOW_RELEASE";
        case STATE_DEFLATED:     return "DEFLATED";
        default: return "UNKNOWN";
    }
}

/* ---------- Objek/variabel global sistem ---------- */
static tourniquet_state_t g_state = STATE_IDLE;
static butterworth_bpf_t g_bp_filter;
static peak_detector_t g_peak_detector = {0};
static pid_controller_t g_pid;

static float g_lop_target_mmhg = 0.0f;
static float g_setpoint_mmhg = 180.0f;
static bool g_is_upper_limb = true;

static int64_t g_state_entered_us = 0;
static int64_t g_holding_start_us = 0;
static int64_t g_last_monitor_check_us = 0;
static int64_t g_last_usage_alarm_us = 0;

static const int SAMPLES_PER_WINDOW = ADC_SAMPLE_RATE_HZ * SAMPLING_WINDOW_SEC;
static int g_sample_counter = 0;
static int64_t g_last_sample_us = 0;
static const int64_t SAMPLE_INTERVAL_US = 1000000 / ADC_SAMPLE_RATE_HZ;

static float g_last_known_pressure = 0.0f;

static void transition_to(tourniquet_state_t new_state) {
    ESP_LOGI(TAG, "[STATE] %s -> %s", state_to_string(g_state), state_to_string(new_state));
    g_state = new_state;
    g_state_entered_us = esp_timer_get_time();
}

static bool button_pressed(gpio_num_t pin) {
    /* active LOW (pull-up internal via config di app_main) */
    if (gpio_get_level(pin) == 0) {
        vTaskDelay(pdMS_TO_TICKS(50)); /* debounce sederhana */
        return gpio_get_level(pin) == 0;
    }
    return false;
}

/* ---------- Handler tiap fase state machine ---------- */

static void handle_idle(void) {
    ui_show_message("Smart Tourniquet", "Tekan POWER");
    transition_to(STATE_SELF_CHECK);
}

static void handle_self_check(void) {
    ui_show_message("Smart Tourniquet", "Self-check...");
    ui_status_ok();
    vTaskDelay(pdMS_TO_TICKS(2000)); /* self-check 2 detik sesuai proposal Fase 0 */
    ui_show_message("SIAP", "Tekan MULAI");
    transition_to(STATE_ARMED);
}

static void handle_armed(void) {
    if (button_pressed(BTN_START_PIN)) {
        g_setpoint_mmhg = g_is_upper_limb ? 180.0f : 250.0f;
        butterworth_reset(&g_bp_filter);
        peak_detector_reset(&g_peak_detector);
        pid_reset(&g_pid);
        g_sample_counter = 0;
        transition_to(STATE_INFLATING);
    }
}

static void handle_inflating(void) {
    float pressure = pressure_read_mmhg(0);
    g_last_known_pressure = pressure;

    if (pressure >= HARD_PRESSURE_LIMIT_MMHG) {
        actuator_pump_off();
        ui_show_message("!! OVER LIMIT !!", "Cek sistem");
        ui_status_critical();
        return;
    }

    uint8_t duty = pid_compute(&g_pid, g_setpoint_mmhg, pressure);
    actuator_set_pump_duty(duty);

    int64_t elapsed_sec = (esp_timer_get_time() - g_state_entered_us) / 1000000;
    ui_show_pressure_status(pressure, state_to_string(g_state), elapsed_sec);

    if (fabsf(pressure - g_setpoint_mmhg) < 3.0f) {
        actuator_pump_off();
        butterworth_reset(&g_bp_filter);
        peak_detector_reset(&g_peak_detector);
        g_sample_counter = 0;
        g_last_sample_us = esp_timer_get_time();
        transition_to(STATE_SAMPLING);
    }
}

static void handle_sampling(void) {
    int64_t now_us = esp_timer_get_time();
    if (now_us - g_last_sample_us >= SAMPLE_INTERVAL_US) {
        g_last_sample_us = now_us;

        float pressure = pressure_read_mmhg(0);
        g_last_known_pressure = pressure;

        float filtered = butterworth_process(&g_bp_filter, pressure);
        peak_detector_add_sample(&g_peak_detector, filtered);
        g_sample_counter++;

        int64_t elapsed_sec = (now_us - g_state_entered_us) / 1000000;
        ui_show_pressure_status(pressure, state_to_string(g_state), elapsed_sec);
    }

    if (g_sample_counter >= SAMPLES_PER_WINDOW) {
        int peaks = g_peak_detector.peak_count;
        ESP_LOGI(TAG, "[SAMPLING] Peaks terdeteksi: %d (threshold: %d)", peaks, PEAK_THRESHOLD_MIN_COUNT);

        if (peaks > PEAK_THRESHOLD_MIN_COUNT) {
            g_setpoint_mmhg += 20.0f;
            float max_target = g_is_upper_limb ? LOP_TARGET_MAX_UPPER_LIMB : LOP_TARGET_MAX_LOWER_LIMB;
            if (g_setpoint_mmhg > max_target) g_setpoint_mmhg = max_target;
            transition_to(STATE_INFLATING);
        } else {
            g_lop_target_mmhg = g_last_known_pressure;
            transition_to(STATE_LOP_DETECTED);
        }
    }
}

static void handle_lop_detected(void) {
    actuator_pump_off();
    actuator_lock_pressure(true);
    ui_show_message("OKLUSI TERCAPAI", "");
    ui_buzzer_beep_short();
    ui_status_ok();
    ESP_LOGI(TAG, "[LOP] Tekanan LOP tercapai: %.1f mmHg", g_lop_target_mmhg);

    g_holding_start_us = esp_timer_get_time();
    g_last_monitor_check_us = g_holding_start_us;
    g_last_usage_alarm_us = g_holding_start_us;
    transition_to(STATE_HOLDING);
}

static void handle_holding(void) {
    float pressure = pressure_read_mmhg(0);
    g_last_known_pressure = pressure;

    int64_t elapsed_sec = (esp_timer_get_time() - g_holding_start_us) / 1000000;
    ui_show_pressure_status(pressure, state_to_string(g_state), elapsed_sec);

    if (button_pressed(BTN_RELEASE_PIN)) {
        transition_to(STATE_SLOW_RELEASE);
        return;
    }

    if (esp_timer_get_time() - g_last_monitor_check_us >= (int64_t)MONITOR_INTERVAL_SEC * 1000000) {
        g_last_monitor_check_us = esp_timer_get_time();
        transition_to(STATE_MONITORING);
    }
}

static void handle_monitoring(void) {
    float pressure = pressure_read_mmhg(0);
    g_last_known_pressure = pressure;

    float drop = g_lop_target_mmhg - pressure;
    int64_t elapsed_sec = (esp_timer_get_time() - g_holding_start_us) / 1000000;
    ui_show_pressure_status(pressure, state_to_string(g_state), elapsed_sec);

    if (esp_timer_get_time() - g_last_usage_alarm_us >= (int64_t)USAGE_ALARM_INTERVAL_MIN * 60 * 1000000) {
        g_last_usage_alarm_us = esp_timer_get_time();
        ui_buzzer_beep_short();
        ui_show_message("EVALUASI MEDIS", "diperlukan segera");
        vTaskDelay(pdMS_TO_TICKS(1500));
    }

    if (drop > PRESSURE_DROP_LEAK_MMHG) {
        transition_to(STATE_ALARM);
        return;
    }

    if (drop > PRESSURE_DROP_REINFLATE_MMHG) {
        ui_show_message("SEDANG", "DIKENCANGKAN ULANG");
        ui_buzzer_beep_short();
        actuator_lock_pressure(false);
        g_setpoint_mmhg = g_lop_target_mmhg;
        pid_reset(&g_pid);
        transition_to(STATE_INFLATING);
        return;
    }

    transition_to(STATE_HOLDING);
}

static void handle_alarm(void) {
    ui_show_message("!! CUFF BOCOR !!", "KENCANGKAN STRAP");
    ui_buzzer_on();
    /* blink LED merah cepat */
    static bool led_state = false;
    led_state = !led_state;
    gpio_set_level(LED_RED_PIN, led_state);
    vTaskDelay(pdMS_TO_TICKS(200));

    if (button_pressed(BTN_START_PIN)) {
        ui_buzzer_off();
        g_setpoint_mmhg = g_lop_target_mmhg;
        pid_reset(&g_pid);
        transition_to(STATE_INFLATING);
        return;
    }
    if (button_pressed(BTN_RELEASE_PIN)) {
        ui_buzzer_off();
        transition_to(STATE_SLOW_RELEASE);
    }
}

static void handle_slow_release(void) {
    static int64_t last_step_us = 0;
    const int64_t step_interval_us = 1000000;
    const float release_per_second = SLOW_RELEASE_RATE_MMHG_PER_MIN / 60.0f;

    ui_show_message("T-CONVERSION", "Slow release...");
    actuator_release_slow(true);

    int64_t now_us = esp_timer_get_time();
    if (now_us - last_step_us >= step_interval_us) {
        last_step_us = now_us;
        float pressure = pressure_read_mmhg(0);
        g_last_known_pressure = pressure;

        ESP_LOGI(TAG, "[SLOW_RELEASE] Tekanan: %.1f mmHg (target turun %.2f mmHg/s)",
                 pressure, release_per_second);

        if (pressure <= 10.0f) {
            actuator_release_slow(false);
            transition_to(STATE_DEFLATED);
        }
    }
}

static void handle_deflated(void) {
    ui_show_message("CUFF DEFLATE", "SEMPURNA");
    actuator_emergency_deflate();
    ui_status_ok();
    /* sistem berhenti di sini -- perlu restart manual untuk siklus baru */
}

/* ============================================================
 * BAGIAN 9: app_main (entry point ESP-IDF, pengganti setup()+loop())
 * ============================================================ */

void app_main(void) {
    ESP_LOGI(TAG, "Smart Tourniquet System — booting...");

    i2c_master_init();

    if (!pressure_sensor_init()) {
        ESP_LOGE(TAG, "ERROR: ADS1115 tidak terdeteksi.");
    }
    ui_init();
    actuator_init();

    gpio_set_direction(BTN_START_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_START_PIN, GPIO_PULLUP_ONLY);
    gpio_set_direction(BTN_RELEASE_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BTN_RELEASE_PIN, GPIO_PULLUP_ONLY);

    butterworth_init(&g_bp_filter);
    peak_detector_reset(&g_peak_detector);
    pid_init(&g_pid, 2.0f, 0.5f, 0.1f); /* Kp/Ki/Kd TITIK AWAL -- wajib tuning ulang, lihat pid_controller di Docs */

    transition_to(STATE_IDLE);

    while (1) {
        switch (g_state) {
            case STATE_IDLE:         handle_idle(); break;
            case STATE_SELF_CHECK:   handle_self_check(); break;
            case STATE_ARMED:        handle_armed(); break;
            case STATE_INFLATING:    handle_inflating(); break;
            case STATE_SAMPLING:     handle_sampling(); break;
            case STATE_LOP_DETECTED: handle_lop_detected(); break;
            case STATE_HOLDING:      handle_holding(); break;
            case STATE_MONITORING:   handle_monitoring(); break;
            case STATE_ALARM:        handle_alarm(); break;
            case STATE_SLOW_RELEASE: handle_slow_release(); break;
            case STATE_DEFLATED:     handle_deflated(); break;
        }
        vTaskDelay(pdMS_TO_TICKS(20)); /* ~50Hz loop rate dasar */
    }
}
