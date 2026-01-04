#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_mac.h"

#include "ssd1306.h"
#include "ssd1306_const.h"

typedef struct {
    float temperature;
    float humidity;
    float pressure;
} sensor_data_t;

static sensor_data_t rx_data;
static volatile bool data_ready = false;

esp_err_t ssd1306_print_float(uint8_t x, uint8_t y, float value, uint8_t decimals, bool invert);


const uint8_t ICON_TEMP_16x16[] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0xfe, 0x01, 0xf1, 0xfe, 0x00, 0x00, 0xd4, 0x00, 0x00, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x18, 0x66, 0x9b, 0xac, 0xa7, 0x99, 0x46, 0x38, 0x00, 0x00, 0x00, 0x00, 0x00
};

const uint8_t celsius_16x16 [] = {
    0x00, 0x00, 0x18, 0x3c, 0x3c, 0x18, 0xc0, 0xe0, 0x70, 0x30, 0x18, 0x18, 0x30, 0x10, 0x00, 0x00, 
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0x0f, 0x1c, 0x18, 0x18, 0x18, 0x18, 0x10, 0x00, 0x00
};

const uint8_t ICON_HUMI_16x16[] = {
    0x00, 0xc0, 0xe0, 0xc0, 0xc0, 0x60, 0x38, 0x18, 0x30, 0xf8, 0x8c, 0x06, 0x1e, 0x2c, 0xf8, 0xe0, 
    0x07, 0x07, 0x0c, 0x3f, 0x67, 0x40, 0xc0, 0xe0, 0xc0, 0x4c, 0x73, 0x1f, 0x06, 0x03, 0x01, 0x00
};

const uint8_t ICON_PRESS_16x16[] = {
    0xe0, 0x08, 0xe4, 0x32, 0x08, 0x0d, 0x05, 0x05, 0x05, 0x05, 0x0d, 0x08, 0x32, 0xe4, 0x08, 0xe0, 
    0x07, 0x10, 0x20, 0x40, 0x00, 0x80, 0x80, 0xcb, 0xcb, 0x80, 0x80, 0x00, 0x40, 0x20, 0x10, 0x07
};

void weather_ui_display(float temp, float humidity, float pressure) 
{
    ssd1306_clear();
    
    ssd1306_print_str(5, 0, "WEATHER STATION", false);
    
    ssd1306_draw_image(0, 16, ICON_TEMP_16x16, 16, 16, false);
    ssd1306_draw_image(40, 16, celsius_16x16, 16, 16, false);
    char temp_str[16];
    sprintf(temp_str, "%.0f", temp);
    ssd1306_print_str(20, 20, temp_str, false);

    ssd1306_draw_image(72, 16, ICON_HUMI_16x16, 16, 16, false);
    char hum_str[16];
    sprintf(hum_str, "%.0f%%", humidity);
    ssd1306_print_str(92, 20, hum_str, false);
    
    ssd1306_draw_image(0, 36, ICON_PRESS_16x16, 16, 16, false);
    char press_str[24];
    sprintf(press_str, "%.0f hPa", pressure);
    ssd1306_print_str(20, 40, press_str, false);

    ssd1306_display();
}

void espnow_recv_cb(const esp_now_recv_info_t *info,
                    const uint8_t *data, int data_len)
{
    ESP_LOGI("ESP_NOW_RECV", "From %02X:%02X:%02X:%02X:%02X:%02X, len=%d",
             info->src_addr[0], info->src_addr[1], info->src_addr[2],
             info->src_addr[3], info->src_addr[4], info->src_addr[5],
             data_len);

    if (data_len == sizeof(sensor_data_t)) {
        memcpy(&rx_data, data, sizeof(sensor_data_t));
        data_ready = true;
    }
}

void wifi_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_ERROR_CHECK(esp_wifi_set_channel(11, WIFI_SECOND_CHAN_NONE));
}

void oled_task(void *pvParameter)
{
    init_ssd1306();
    while (1) {
        if (!data_ready) {
            ssd1306_clear();
            ssd1306_print_str(0, 24, "Waiting for data", false);
            ssd1306_print_str(50, 40, "...", false);
            ssd1306_display();
        } else {
            data_ready = false;
            weather_ui_display(rx_data.temperature, rx_data.humidity, rx_data.pressure);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void app_main(void)
{
    wifi_init();

    ESP_ERROR_CHECK(esp_now_init());
    ESP_ERROR_CHECK(esp_now_register_recv_cb(espnow_recv_cb));

    xTaskCreate(&oled_task, "oled_task", 4096, NULL, 5, NULL);
}

static void ssd1306_float_to_str(char *out, size_t size, float v, uint8_t decimals)
{
    if (size == 0) return;

    if (isnan(v)) {
        snprintf(out, size, "nan");
        return;
    }
    if (isinf(v)) {
        if (v < 0) snprintf(out, size, "-inf");
        else snprintf(out, size, "inf");
        return;
    }

    bool neg = (v < 0.0f);
    if (neg) v = -v;

    int ipart = (int)v;
    float fracf = v - (float)ipart;

    int mult = 1;
    for (uint8_t i = 0; i < decimals; i++) mult *= 10;
    int fpart = (int)(fracf * mult + 0.5f);

    if (fpart >= mult) {
        ipart += 1;
        fpart -= mult;
    }

    if (decimals == 0) {
        if (neg) snprintf(out, size, "-%d", ipart);
        else snprintf(out, size, "%d", ipart);
        return;
    }

    char frac_buf[16];

    snprintf(frac_buf, sizeof(frac_buf), "%0*d", decimals, fpart);

    if (neg) snprintf(out, size, "-%d.%s", ipart, frac_buf);
    else snprintf(out, size, "%d.%s", ipart, frac_buf);
}

esp_err_t ssd1306_print_float(uint8_t x, uint8_t y, float value, uint8_t decimals, bool invert)
{
    char buf[32];
    ssd1306_float_to_str(buf, sizeof(buf), value, decimals);

    return ssd1306_print_str(x, y, buf, invert);
}