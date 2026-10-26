#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/i2c.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_err.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "time.h"
#include "qrcode.h"
#include "wifi.h"

static const char *TAG = "oled";

#define I2C_HOST          I2C_NUM_0
#define PIN_SDA           21
#define PIN_SCL           22
#define I2C_CLK_HZ        400000
#define OLED_ADDR         0x3C      
#define OLED_WIDTH        128
#define OLED_HEIGHT       64

#define RED_LED GPIO_NUM_13
#define YELLOW_LED GPIO_NUM_14
#define GREEN_LED GPIO_NUM_27

/* SSD1306 memory layout: 8 "pages" of 8 vertical pixels each.
 * One byte = one column of 8 pixels, bit 0 at the top. */
static uint8_t s_fb[OLED_WIDTH * OLED_HEIGHT / 8];

/* ---------- 5x7 font, ASCII 0x20 (' ') to 0x7E ('~') ---------- */
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, {0x00,0x00,0x5F,0x00,0x00}, {0x00,0x07,0x00,0x07,0x00}, {0x14,0x7F,0x14,0x7F,0x14}, // ' ' ! " #
    {0x24,0x2A,0x7F,0x2A,0x12}, {0x23,0x13,0x08,0x64,0x62}, {0x36,0x49,0x55,0x22,0x50}, {0x00,0x05,0x03,0x00,0x00}, // $ % & '
    {0x00,0x1C,0x22,0x41,0x00}, {0x00,0x41,0x22,0x1C,0x00}, {0x08,0x2A,0x1C,0x2A,0x08}, {0x08,0x08,0x3E,0x08,0x08}, // ( ) * +
    {0x00,0x50,0x30,0x00,0x00}, {0x08,0x08,0x08,0x08,0x08}, {0x00,0x60,0x60,0x00,0x00}, {0x20,0x10,0x08,0x04,0x02}, // , - . /
    {0x3E,0x51,0x49,0x45,0x3E}, {0x00,0x42,0x7F,0x40,0x00}, {0x42,0x61,0x51,0x49,0x46}, {0x21,0x41,0x45,0x4B,0x31}, // 0 1 2 3
    {0x18,0x14,0x12,0x7F,0x10}, {0x27,0x45,0x45,0x45,0x39}, {0x3C,0x4A,0x49,0x49,0x30}, {0x01,0x71,0x09,0x05,0x03}, // 4 5 6 7
    {0x36,0x49,0x49,0x49,0x36}, {0x06,0x49,0x49,0x29,0x1E}, {0x00,0x36,0x36,0x00,0x00}, {0x00,0x56,0x36,0x00,0x00}, // 8 9 : ;
    {0x08,0x14,0x22,0x41,0x00}, {0x14,0x14,0x14,0x14,0x14}, {0x00,0x41,0x22,0x14,0x08}, {0x02,0x01,0x51,0x09,0x06}, // < = > ?
    {0x32,0x49,0x79,0x41,0x3E}, {0x7E,0x11,0x11,0x11,0x7E}, {0x7F,0x49,0x49,0x49,0x36}, {0x3E,0x41,0x41,0x41,0x22}, // @ A B C
    {0x7F,0x41,0x41,0x22,0x1C}, {0x7F,0x49,0x49,0x49,0x41}, {0x7F,0x09,0x09,0x01,0x01}, {0x3E,0x41,0x41,0x51,0x32}, // D E F G
    {0x7F,0x08,0x08,0x08,0x7F}, {0x00,0x41,0x7F,0x41,0x00}, {0x20,0x40,0x41,0x3F,0x01}, {0x7F,0x08,0x14,0x22,0x41}, // H I J K
    {0x7F,0x40,0x40,0x40,0x40}, {0x7F,0x02,0x04,0x02,0x7F}, {0x7F,0x04,0x08,0x10,0x7F}, {0x3E,0x41,0x41,0x41,0x3E}, // L M N O
    {0x7F,0x09,0x09,0x09,0x06}, {0x3E,0x41,0x51,0x21,0x5E}, {0x7F,0x09,0x19,0x29,0x46}, {0x46,0x49,0x49,0x49,0x31}, // P Q R S
    {0x01,0x01,0x7F,0x01,0x01}, {0x3F,0x40,0x40,0x40,0x3F}, {0x1F,0x20,0x40,0x20,0x1F}, {0x7F,0x20,0x18,0x20,0x7F}, // T U V W
    {0x63,0x14,0x08,0x14,0x63}, {0x03,0x04,0x78,0x04,0x03}, {0x61,0x51,0x49,0x45,0x43}, {0x00,0x7F,0x41,0x41,0x00}, // X Y Z [
    {0x02,0x04,0x08,0x10,0x20}, {0x00,0x41,0x41,0x7F,0x00}, {0x04,0x02,0x01,0x02,0x04}, {0x40,0x40,0x40,0x40,0x40}, // \ ] ^ _
    {0x00,0x01,0x02,0x04,0x00}, {0x20,0x54,0x54,0x54,0x78}, {0x7F,0x48,0x44,0x44,0x38}, {0x38,0x44,0x44,0x44,0x20}, // ` a b c
    {0x38,0x44,0x44,0x48,0x7F}, {0x38,0x54,0x54,0x54,0x18}, {0x08,0x7E,0x09,0x01,0x02}, {0x08,0x14,0x54,0x54,0x3C}, // d e f g
    {0x7F,0x08,0x04,0x04,0x78}, {0x00,0x44,0x7D,0x40,0x00}, {0x20,0x40,0x44,0x3D,0x00}, {0x00,0x7F,0x10,0x28,0x44}, // h i j k
    {0x00,0x41,0x7F,0x40,0x00}, {0x7C,0x04,0x18,0x04,0x78}, {0x7C,0x08,0x04,0x04,0x78}, {0x38,0x44,0x44,0x44,0x38}, // l m n o
    {0x7C,0x14,0x14,0x14,0x08}, {0x08,0x14,0x14,0x18,0x7C}, {0x7C,0x08,0x04,0x04,0x08}, {0x48,0x54,0x54,0x54,0x20}, // p q r s
    {0x04,0x3F,0x44,0x40,0x20}, {0x3C,0x40,0x40,0x20,0x7C}, {0x1C,0x20,0x40,0x20,0x1C}, {0x3C,0x40,0x30,0x40,0x3C}, // t u v w
    {0x44,0x28,0x10,0x28,0x44}, {0x0C,0x50,0x50,0x50,0x3C}, {0x44,0x64,0x54,0x4C,0x44}, {0x00,0x08,0x36,0x41,0x00}, // x y z {
    {0x00,0x00,0x7F,0x00,0x00}, {0x00,0x41,0x36,0x08,0x00}, {0x02,0x01,0x02,0x04,0x02},                             // | } ~
};

static void fb_clear(void)
{
    memset(s_fb, 0, sizeof(s_fb));
}

static void fb_set_pixel(int x, int y, bool on)
{
    if (x < 0 || x >= OLED_WIDTH || y < 0 || y >= OLED_HEIGHT) {
        return;
    }
    uint8_t *byte = &s_fb[(y / 8) * OLED_WIDTH + x];
    if (on) {
        *byte |= (1 << (y % 8));
    } else {
        *byte &= ~(1 << (y % 8));
    }
}

static void fb_draw_rect(int x, int y, int w, int h)
{
    for (int i = x; i < x + w; i++) {
        fb_set_pixel(i, y, true);
        fb_set_pixel(i, y + h - 1, true);
    }
    for (int j = y; j < y + h; j++) {
        fb_set_pixel(x, j, true);
        fb_set_pixel(x + w - 1, j, true);
    }
}

/* Filled rectangle; on = false erases the area instead */
static void fb_fill_rect(int x, int y, int w, int h, bool on)
{
    for (int i = x; i < x + w; i++) {
        for (int j = y; j < y + h; j++) {
            fb_set_pixel(i, j, on);
        }
    }
}

static void fb_draw_wifi_bars(int x, int y, int level)
{
    const int bars = 4, bar_w = 5, gap = 2, total_h = 20;

    if (level < 0) level = 0;
    if (level > bars) level = bars;

    for (int i = 0; i < bars; i++) {
        int bar_h = total_h * (i + 1) / bars;   
        int bx = x + i * (bar_w + gap);
        int by = y + total_h - bar_h;          
        if (i < level) {
            fb_fill_rect(bx, by, bar_w, bar_h, true);
        } else {
            fb_draw_rect(bx, by, bar_w, bar_h);
        }
    }
}

void led_set(int target_gpio) {
    gpio_set_level(RED_LED,    (target_gpio == RED_LED));
    gpio_set_level(YELLOW_LED, (target_gpio == YELLOW_LED));
    gpio_set_level(GREEN_LED,  (target_gpio == GREEN_LED));
}


/* Converts an RSSI value in dBm (from esp_wifi_sta_get_ap_info) to 0-4 bars */
static int wifi_rssi_to_level(int rssi)
{
    if (rssi >= -55){
        led_set(GREEN_LED);
        return 4;
    }
    if (rssi >= -65){
        led_set(YELLOW_LED);
        return 3;
    }
    if (rssi >= -75){
        led_set(YELLOW_LED);
        return 2;
    }
    if (rssi >= -85){
        led_set(RED_LED);
        return 1;
    } 
    return 0;
}

static void i2c_scan(void)
{
    for (uint8_t addr = 1; addr < 127; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        if (i2c_master_cmd_begin(I2C_HOST, cmd, pdMS_TO_TICKS(50)) == ESP_OK) ESP_LOGI(TAG, "Found device at 0x%02X", addr);
        i2c_cmd_link_delete(cmd);
    }
}

/* Draws one character; scale 1 = 5x7 pixels, scale 2 = 10x14, etc. */
static void fb_draw_char(int x, int y, char c, int scale)
{
    if (c < 0x20 || c > 0x7E) {
        c = '?';
    }
    const uint8_t *glyph = font5x7[c - 0x20];
    for (int col = 0; col < 5; col++) {
        for (int row = 0; row < 7; row++) {
            if (glyph[col] & (1 << row)) {
                for (int dx = 0; dx < scale; dx++) {
                    for (int dy = 0; dy < scale; dy++) {
                        fb_set_pixel(x + col * scale + dx, y + row * scale + dy, true);
                    }
                }
            }
        }
    }
}

static void fb_draw_string(int x, int y, const char *str, int scale)
{
    while (*str) {
        fb_draw_char(x, y, *str++, scale);
        x += 6 * scale;   /* 5 px glyph + 1 px spacing */
    }
}

#define QR_QUIET_MODULES  1   /* light border around the code, in modules */

/* Scales the code (plus its border) to the full screen height. Nearest-neighbour
 * mapping makes each module 1 or 2 px, so a 33-module code fills 64 px instead
 * of being stuck at 33 px with whole-pixel scaling. */
static void fb_draw_qrcode(esp_qrcode_handle_t qrcode){
    int size = esp_qrcode_get_size(qrcode);
    int total = size + 2 * QR_QUIET_MODULES;      
    int side = OLED_HEIGHT;                       
    int x0 = (OLED_WIDTH - side) / 2;

    fb_fill_rect(0, 0, OLED_WIDTH, OLED_HEIGHT, true);
    for (int py = 0; py < side; py++) {
        int my = py * total / side - QR_QUIET_MODULES;
        if (my < 0 || my >= size) continue;
        for (int px = 0; px < side; px++) {
            int mx = px * total / side - QR_QUIET_MODULES;
            if (mx >= 0 && mx < size && esp_qrcode_get_module(qrcode, mx, my)) {
                fb_set_pixel(x0 + px, py, false);
            }
        }
    }
}

static void show_qrcode(const char *text)
{
    esp_qrcode_config_t cfg = ESP_QRCODE_CONFIG_DEFAULT();
    cfg.display_func = fb_draw_qrcode;
    cfg.max_qrcode_version = 4;          /* 33x33 modules: ~78 bytes, needed for the prov JSON */
    cfg.qrcode_ecc_level = ESP_QRCODE_ECC_LOW;

    if (esp_qrcode_generate(&cfg, text) == ESP_OK) {
    } else {
        ESP_LOGE(TAG, "Text too long for a version %d QR code", cfg.max_qrcode_version);
    }
}


static esp_lcd_panel_handle_t oled_init(void)
{
    ESP_LOGI(TAG, "Initializing I2C bus");
    i2c_config_t i2c_conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = PIN_SDA,
        .scl_io_num = PIN_SCL,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_CLK_HZ,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_HOST, &i2c_conf));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_HOST, I2C_MODE_MASTER, 0, 0, 0));
    i2c_scan();
    ESP_LOGI(TAG, "Installing panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t io_config = {
        .dev_addr = OLED_ADDR,
        .control_phase_bytes = 1,   /* SSD1306 datasheet */
        .dc_bit_offset = 6,         /* SSD1306 datasheet */
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c((esp_lcd_i2c_bus_handle_t)I2C_HOST,
                                             &io_config, &io_handle));

    ESP_LOGI(TAG, "Installing SSD1306 driver");
    esp_lcd_panel_handle_t panel = NULL;
    esp_lcd_panel_dev_config_t panel_config = {
        .bits_per_pixel = 1,
        .reset_gpio_num = -1,      
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_ssd1306(io_handle, &panel_config, &panel));

    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));

    /* Rotate 180 degrees (this module's panel is mounted upside down) */
    ESP_ERROR_CHECK(esp_lcd_panel_mirror(panel, true, true));

    return panel;
}

static void oled_flush(esp_lcd_panel_handle_t panel)
{
    ESP_ERROR_CHECK(esp_lcd_panel_draw_bitmap(panel, 0, 0, OLED_WIDTH, OLED_HEIGHT, s_fb));
}

void app_main(void)
{
    esp_lcd_panel_handle_t panel = oled_init();

    char rssi_str[12];
    char ssid[33];
    char time[64];

    gpio_reset_pin(RED_LED);
    gpio_reset_pin(YELLOW_LED);
    gpio_reset_pin(GREEN_LED);

    gpio_set_direction(RED_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(YELLOW_LED, GPIO_MODE_OUTPUT);
    gpio_set_direction(GREEN_LED, GPIO_MODE_OUTPUT);

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI("MAIN", "ESP_IDF version: %s", esp_get_idf_version());
    ESP_LOGI("MAIN", "Initializing Wi-Fi...");
    
    // Connect to Wi-Fi, provisioning first if no credentials are saved
    char qr_payload[100];
    if (wifi_start(qr_payload, sizeof(qr_payload))) {
        fb_clear();
        show_qrcode(qr_payload);
        oled_flush(panel);
    }
    if (!wifi_wait_connected()) {
        fb_clear();
        fb_draw_string(2, 2, "Wi-Fi failed", 1);
        oled_flush(panel);
    }
    initialize_sntp();

    while (1) {
        fb_clear();

        snprintf(ssid, sizeof(ssid), "%s", wifi_ssid());
        fb_draw_string(2, 2, "Wi-Fi SSID:", 1);
        fb_draw_string(2, 14, ssid, 1);

        int rssi = wifi_rssi();
        snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
        fb_draw_string(4, 30, rssi_str, 2);

        int text_w = strlen(rssi_str) * 12;
        fb_draw_string(4 + text_w + 2, 30, "dBm", 2);
        fb_draw_wifi_bars(4 + text_w + 42, 24, wifi_rssi_to_level(rssi));

        snprintf(time, sizeof(time), "%s", print_local_time());
        fb_draw_string(2, 50, time, 1);

        oled_flush(panel);

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}