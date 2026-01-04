
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_now.h"
#include "esp_system.h"
#include "esp_log.h"
#include "mqtt_client.h"

/* Bosch BME280 */
#include "bme280_defs.h"
#include "bme280.h"
#include <esp_err.h>

#define WIFI_SSID "WIFI-SSID"
#define WIFI_PASS "WIFI-PASSWORD"
#define MQTT_URI  "mqtt://ip-address"
#define MQTT_TOPIC "bme280/data"

typedef struct {
    float temperature;
    float humidity;
    float pressure;
} sensor_data_t;

static uint8_t receiver_mac[6] = { 0xA8, 0x46, 0x74, 0xDE, 0x87, 0x5C };
static esp_mqtt_client_handle_t mqtt_client = NULL;
static const char *TAG = "MAIN";

/* ===== MQTT event handler ===== */
static void mqtt_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT disconnected");
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT published, msg_id=%d", event->msg_id);
            break;
        default:
            break;
    }
}

/* ===== WiFi init (STA) ===== */
void wifi_init_sta(void)
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

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            /* threshold optional */
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi started, connecting to AP...");
    ESP_ERROR_CHECK(esp_wifi_connect());
}

/* ===== MQTT init ===== */
void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_URI,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, mqtt_client);

    esp_err_t r = esp_mqtt_client_start(mqtt_client);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(r));
    } else {
        ESP_LOGI(TAG, "MQTT client started");
    }
}

/* ===== ESP-NOW send callback (for status) ===== */
static void espnow_send_cb(const uint8_t *mac_addr, esp_now_send_status_t status)
{
    char macstr[18];
    if (mac_addr) {
        sprintf(macstr, "%02X:%02X:%02X:%02X:%02X:%02X",
                mac_addr[0], mac_addr[1], mac_addr[2],
                mac_addr[3], mac_addr[4], mac_addr[5]);
    } else {
        strcpy(macstr, "NULL");
    }

    ESP_LOGI(TAG, "ESP-NOW send_cb to %s, status=%s",
             macstr,
             (status == ESP_NOW_SEND_SUCCESS) ? "SUCCESS" : "FAILED");
}

/* ===== ESP-NOW init (sender) ===== */
static void espnow_init_sender(void)
{
    esp_err_t ret = esp_now_init();
    if (ret == ESP_ERR_ESPNOW_NOT_INIT || ret == ESP_OK) {
        // ok
    } else {
        ESP_LOGE(TAG, "esp_now_init failed: %s", esp_err_to_name(ret));
        return;
    }

    esp_now_register_send_cb(espnow_send_cb);

    uint8_t primary = 0;
    wifi_second_chan_t second;
    esp_err_t r = esp_wifi_get_channel(&primary, &second);
    if (r != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_get_channel failed: %s, will use channel=0 (auto)", esp_err_to_name(r));
    } else {
        ESP_LOGI(TAG, "Current WiFi channel: %d", primary);
    }

    esp_now_peer_info_t peer_info = {0};
    memcpy(peer_info.peer_addr, receiver_mac, 6);
    peer_info.channel = (r == ESP_OK) ? primary : 0; /* 0 = use current */
#if defined(ESP_IF_WIFI_STA)
    peer_info.ifidx = ESP_IF_WIFI_STA;
#endif
    peer_info.encrypt = false;

    ret = esp_now_add_peer(&peer_info);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ESP-NOW peer added");
    } else if (ret == ESP_ERR_ESPNOW_EXIST) {
        ESP_LOGI(TAG, "ESP-NOW peer already exists");
    } else {
        ESP_LOGE(TAG, "esp_now_add_peer failed: %s", esp_err_to_name(ret));
    }
}

/* ===== ESP-NOW send data ===== */
void espnow_send_data(sensor_data_t *data)
{
    esp_err_t err = esp_now_send(receiver_mac, (uint8_t *)data, sizeof(sensor_data_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_now_send failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "esp_now_send queued");
    }
}

/* ===== I2C config ===== */
#define I2C_PORT        I2C_NUM_0
#define I2C_SDA_PIN     21
#define I2C_SCL_PIN     22
#define I2C_FREQ_HZ     100000

static uint8_t bme280_i2c_addr = 0x76;

/* ===== I2C init ===== */
static void i2c_master_init(void)
{
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_PIN,
        .scl_io_num = I2C_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_FREQ_HZ,
    };
    i2c_param_config(I2C_PORT, &conf);
    esp_err_t r = i2c_driver_install(I2C_PORT, conf.mode, 0, 0, 0);
    if (r != ESP_OK) {
        ESP_LOGE(TAG, "i2c_driver_install failed: %s", esp_err_to_name(r));
    } else {
        ESP_LOGI(TAG, "I2C driver installed");
    }
}

/* ===== BME280 interface functions ===== */
int8_t bme280_i2c_read(uint8_t reg, uint8_t *data, uint32_t len, void *intf_ptr)
{
    uint8_t addr = *(uint8_t *)intf_ptr;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_READ, true);
    if (len > 1)
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    return (ret == ESP_OK) ? BME280_OK : BME280_E_COMM_FAIL;
}

int8_t bme280_i2c_write(uint8_t reg, const uint8_t *data, uint32_t len, void *intf_ptr)
{
    uint8_t addr = *(uint8_t *)intf_ptr;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);

    return (ret == ESP_OK) ? BME280_OK : BME280_E_COMM_FAIL;
}

void bme280_delay_us(uint32_t period, void *intf_ptr)
{
    esp_rom_delay_us(period);
}

/* ===== ESP-NOW sender task ===== */
void espnow_sender_task(void *pvParameters)
{
    char payload[128];
    struct bme280_data bme;
    sensor_data_t tx_data;
    struct bme280_dev *dev = (struct bme280_dev *) pvParameters;

    while (1) {
        if (bme280_get_sensor_data(BME280_ALL, &bme, dev) != BME280_OK) {
            ESP_LOGE(TAG, "BME280 read failed");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }

        tx_data.temperature = bme.temperature;
        tx_data.humidity    = bme.humidity;
        tx_data.pressure    = bme.pressure / 100.0f;

        espnow_send_data(&tx_data);

        if (mqtt_client) {
            int len = snprintf(payload, sizeof(payload),
                     "{\"temperature\": %.2f, \"humidity\": %.2f, \"pressure\": %.2f}",
                     tx_data.temperature, tx_data.humidity, tx_data.pressure);

            esp_mqtt_client_publish(mqtt_client, MQTT_TOPIC, payload, len, 1, 0);
            ESP_LOGI(TAG, "MQTT publish: %s", payload);
        }

        ESP_LOGI(TAG, "SENSOR T=%.2f H=%.2f P=%.2f hPa",
                 tx_data.temperature, tx_data.humidity, tx_data.pressure);

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

/* ===== MAIN ===== */
void app_main(void)
{
    static struct bme280_dev dev;
    struct bme280_settings settings;

    i2c_master_init();

    wifi_init_sta();

    vTaskDelay(pdMS_TO_TICKS(4000));

    mqtt_init();

    espnow_init_sender();

    /* Bind BME280 */
    dev.intf = BME280_I2C_INTF;
    dev.read = bme280_i2c_read;
    dev.write = bme280_i2c_write;
    dev.delay_us = bme280_delay_us;
    dev.intf_ptr = &bme280_i2c_addr;

    if (bme280_init(&dev) != BME280_OK) {
        ESP_LOGE(TAG, "BME280 init failed");
        return;
    }

    settings.osr_h = BME280_OVERSAMPLING_1X;
    settings.osr_p = BME280_OVERSAMPLING_1X;
    settings.osr_t = BME280_OVERSAMPLING_1X;
    settings.filter = BME280_FILTER_COEFF_OFF;

    bme280_set_sensor_settings(
        BME280_SEL_OSR_PRESS |
        BME280_SEL_OSR_TEMP  |
        BME280_SEL_OSR_HUM,
        &settings,
        &dev);

    bme280_set_sensor_mode(BME280_POWERMODE_NORMAL, &dev);

    xTaskCreate(espnow_sender_task, "espnow_sender", 4096, &dev, 5, NULL);
}
