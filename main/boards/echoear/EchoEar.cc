#include "EchoEar.h"
#include "wifi_board.h"
#include "codecs/box_audio_codec.h"
#include "display/lcd_display.h"
#include "display/emote_display.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "backlight.h"
#include "charge.h"
#include "touch_cst816s.h"
#include <esp_lcd_touch.h>
#include <esp_lv_adapter.h>
#include <lvgl.h>
#include "base_control.h"
#include "audio_analysis.h"
#include "echoear_tools.h"
#include "touch_sensor.h"
#include "ui_bridge.h"

#include <wifi_station.h>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/i2c.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <esp_psram.h>
#include <esp_lcd_st77916.h>
#include <driver/temperature_sensor.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <functional>

#define TAG "EchoEar"

temperature_sensor_handle_t temp_sensor = NULL;

// ST77916 LCD initialization commands
static const st77916_lcd_init_cmd_t vendor_specific_init_yysj[] = {
    {0xF0, (uint8_t []){0x28}, 1, 0}, {0xF2, (uint8_t []){0x28}, 1, 0}, {0x73, (uint8_t []){0xF0}, 1, 0},
    {0x7C, (uint8_t []){0xD1}, 1, 0}, {0x83, (uint8_t []){0xE0}, 1, 0}, {0x84, (uint8_t []){0x61}, 1, 0},
    {0xF2, (uint8_t []){0x82}, 1, 0}, {0xF0, (uint8_t []){0x00}, 1, 0}, {0xF0, (uint8_t []){0x01}, 1, 0},
    {0xF1, (uint8_t []){0x01}, 1, 0}, {0xB0, (uint8_t []){0x56}, 1, 0}, {0xB1, (uint8_t []){0x4D}, 1, 0},
    {0xB2, (uint8_t []){0x24}, 1, 0}, {0xB4, (uint8_t []){0x87}, 1, 0}, {0xB5, (uint8_t []){0x44}, 1, 0},
    {0xB6, (uint8_t []){0x8B}, 1, 0}, {0xB7, (uint8_t []){0x40}, 1, 0}, {0xB8, (uint8_t []){0x86}, 1, 0},
    {0xBA, (uint8_t []){0x00}, 1, 0}, {0xBB, (uint8_t []){0x08}, 1, 0}, {0xBC, (uint8_t []){0x08}, 1, 0},
    {0xBD, (uint8_t []){0x00}, 1, 0}, {0xC0, (uint8_t []){0x80}, 1, 0}, {0xC1, (uint8_t []){0x10}, 1, 0},
    {0xC2, (uint8_t []){0x37}, 1, 0}, {0xC3, (uint8_t []){0x80}, 1, 0}, {0xC4, (uint8_t []){0x10}, 1, 0},
    {0xC5, (uint8_t []){0x37}, 1, 0}, {0xC6, (uint8_t []){0xA9}, 1, 0}, {0xC7, (uint8_t []){0x41}, 1, 0},
    {0xC8, (uint8_t []){0x01}, 1, 0}, {0xC9, (uint8_t []){0xA9}, 1, 0}, {0xCA, (uint8_t []){0x41}, 1, 0},
    {0xCB, (uint8_t []){0x01}, 1, 0}, {0xD0, (uint8_t []){0x91}, 1, 0}, {0xD1, (uint8_t []){0x68}, 1, 0},
    {0xD2, (uint8_t []){0x68}, 1, 0}, {0xF5, (uint8_t []){0x00, 0xA5}, 2, 0}, {0xDD, (uint8_t []){0x4F}, 1, 0},
    {0xDE, (uint8_t []){0x4F}, 1, 0}, {0xF1, (uint8_t []){0x10}, 1, 0}, {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xF0, (uint8_t []){0x02}, 1, 0},
    {0xE0, (uint8_t []){0xF0, 0x0A, 0x10, 0x09, 0x09, 0x36, 0x35, 0x33, 0x4A, 0x29, 0x15, 0x15, 0x2E, 0x34}, 14, 0},
    {0xE1, (uint8_t []){0xF0, 0x0A, 0x0F, 0x08, 0x08, 0x05, 0x34, 0x33, 0x4A, 0x39, 0x15, 0x15, 0x2D, 0x33}, 14, 0},
    {0xF0, (uint8_t []){0x10}, 1, 0}, {0xF3, (uint8_t []){0x10}, 1, 0}, {0xE0, (uint8_t []){0x07}, 1, 0},
    {0xE1, (uint8_t []){0x00}, 1, 0}, {0xE2, (uint8_t []){0x00}, 1, 0}, {0xE3, (uint8_t []){0x00}, 1, 0},
    {0xE4, (uint8_t []){0xE0}, 1, 0}, {0xE5, (uint8_t []){0x06}, 1, 0}, {0xE6, (uint8_t []){0x21}, 1, 0},
    {0xE7, (uint8_t []){0x01}, 1, 0}, {0xE8, (uint8_t []){0x05}, 1, 0}, {0xE9, (uint8_t []){0x02}, 1, 0},
    {0xEA, (uint8_t []){0xDA}, 1, 0}, {0xEB, (uint8_t []){0x00}, 1, 0}, {0xEC, (uint8_t []){0x00}, 1, 0},
    {0xED, (uint8_t []){0x0F}, 1, 0}, {0xEE, (uint8_t []){0x00}, 1, 0}, {0xEF, (uint8_t []){0x00}, 1, 0},
    {0xF8, (uint8_t []){0x00}, 1, 0}, {0xF9, (uint8_t []){0x00}, 1, 0}, {0xFA, (uint8_t []){0x00}, 1, 0},
    {0xFB, (uint8_t []){0x00}, 1, 0}, {0xFC, (uint8_t []){0x00}, 1, 0}, {0xFD, (uint8_t []){0x00}, 1, 0},
    {0xFE, (uint8_t []){0x00}, 1, 0}, {0xFF, (uint8_t []){0x00}, 1, 0}, {0x60, (uint8_t []){0x40}, 1, 0},
    {0x61, (uint8_t []){0x04}, 1, 0}, {0x62, (uint8_t []){0x00}, 1, 0}, {0x63, (uint8_t []){0x42}, 1, 0},
    {0x64, (uint8_t []){0xD9}, 1, 0}, {0x65, (uint8_t []){0x00}, 1, 0}, {0x66, (uint8_t []){0x00}, 1, 0},
    {0x67, (uint8_t []){0x00}, 1, 0}, {0x68, (uint8_t []){0x00}, 1, 0}, {0x69, (uint8_t []){0x00}, 1, 0},
    {0x6A, (uint8_t []){0x00}, 1, 0}, {0x6B, (uint8_t []){0x00}, 1, 0}, {0x70, (uint8_t []){0x40}, 1, 0},
    {0x71, (uint8_t []){0x03}, 1, 0}, {0x72, (uint8_t []){0x00}, 1, 0}, {0x73, (uint8_t []){0x42}, 1, 0},
    {0x74, (uint8_t []){0xD8}, 1, 0}, {0x75, (uint8_t []){0x00}, 1, 0}, {0x76, (uint8_t []){0x00}, 1, 0},
    {0x77, (uint8_t []){0x00}, 1, 0}, {0x78, (uint8_t []){0x00}, 1, 0}, {0x79, (uint8_t []){0x00}, 1, 0},
    {0x7A, (uint8_t []){0x00}, 1, 0}, {0x7B, (uint8_t []){0x00}, 1, 0}, {0x80, (uint8_t []){0x48}, 1, 0},
    {0x81, (uint8_t []){0x00}, 1, 0}, {0x82, (uint8_t []){0x06}, 1, 0}, {0x83, (uint8_t []){0x02}, 1, 0},
    {0x84, (uint8_t []){0xD6}, 1, 0}, {0x85, (uint8_t []){0x04}, 1, 0}, {0x86, (uint8_t []){0x00}, 1, 0},
    {0x87, (uint8_t []){0x00}, 1, 0}, {0x88, (uint8_t []){0x48}, 1, 0}, {0x89, (uint8_t []){0x00}, 1, 0},
    {0x8A, (uint8_t []){0x08}, 1, 0}, {0x8B, (uint8_t []){0x02}, 1, 0}, {0x8C, (uint8_t []){0xD8}, 1, 0},
    {0x8D, (uint8_t []){0x04}, 1, 0}, {0x8E, (uint8_t []){0x00}, 1, 0}, {0x8F, (uint8_t []){0x00}, 1, 0},
    {0x90, (uint8_t []){0x48}, 1, 0}, {0x91, (uint8_t []){0x00}, 1, 0}, {0x92, (uint8_t []){0x0A}, 1, 0},
    {0x93, (uint8_t []){0x02}, 1, 0}, {0x94, (uint8_t []){0xDA}, 1, 0}, {0x95, (uint8_t []){0x04}, 1, 0},
    {0x96, (uint8_t []){0x00}, 1, 0}, {0x97, (uint8_t []){0x00}, 1, 0}, {0x98, (uint8_t []){0x48}, 1, 0},
    {0x99, (uint8_t []){0x00}, 1, 0}, {0x9A, (uint8_t []){0x0C}, 1, 0}, {0x9B, (uint8_t []){0x02}, 1, 0},
    {0x9C, (uint8_t []){0xDC}, 1, 0}, {0x9D, (uint8_t []){0x04}, 1, 0}, {0x9E, (uint8_t []){0x00}, 1, 0},
    {0x9F, (uint8_t []){0x00}, 1, 0}, {0xA0, (uint8_t []){0x48}, 1, 0}, {0xA1, (uint8_t []){0x00}, 1, 0},
    {0xA2, (uint8_t []){0x05}, 1, 0}, {0xA3, (uint8_t []){0x02}, 1, 0}, {0xA4, (uint8_t []){0xD5}, 1, 0},
    {0xA5, (uint8_t []){0x04}, 1, 0}, {0xA6, (uint8_t []){0x00}, 1, 0}, {0xA7, (uint8_t []){0x00}, 1, 0},
    {0xA8, (uint8_t []){0x48}, 1, 0}, {0xA9, (uint8_t []){0x00}, 1, 0}, {0xAA, (uint8_t []){0x07}, 1, 0},
    {0xAB, (uint8_t []){0x02}, 1, 0}, {0xAC, (uint8_t []){0xD7}, 1, 0}, {0xAD, (uint8_t []){0x04}, 1, 0},
    {0xAE, (uint8_t []){0x00}, 1, 0}, {0xAF, (uint8_t []){0x00}, 1, 0}, {0xB0, (uint8_t []){0x48}, 1, 0},
    {0xB1, (uint8_t []){0x00}, 1, 0}, {0xB2, (uint8_t []){0x09}, 1, 0}, {0xB3, (uint8_t []){0x02}, 1, 0},
    {0xB4, (uint8_t []){0xD9}, 1, 0}, {0xB5, (uint8_t []){0x04}, 1, 0}, {0xB6, (uint8_t []){0x00}, 1, 0},
    {0xB7, (uint8_t []){0x00}, 1, 0}, {0xB8, (uint8_t []){0x48}, 1, 0}, {0xB9, (uint8_t []){0x00}, 1, 0},
    {0xBA, (uint8_t []){0x0B}, 1, 0}, {0xBB, (uint8_t []){0x02}, 1, 0}, {0xBC, (uint8_t []){0xDB}, 1, 0},
    {0xBD, (uint8_t []){0x04}, 1, 0}, {0xBE, (uint8_t []){0x00}, 1, 0}, {0xBF, (uint8_t []){0x00}, 1, 0},
    {0xC0, (uint8_t []){0x10}, 1, 0}, {0xC1, (uint8_t []){0x47}, 1, 0}, {0xC2, (uint8_t []){0x56}, 1, 0},
    {0xC3, (uint8_t []){0x65}, 1, 0}, {0xC4, (uint8_t []){0x74}, 1, 0}, {0xC5, (uint8_t []){0x88}, 1, 0},
    {0xC6, (uint8_t []){0x99}, 1, 0}, {0xC7, (uint8_t []){0x01}, 1, 0}, {0xC8, (uint8_t []){0xBB}, 1, 0},
    {0xC9, (uint8_t []){0xAA}, 1, 0}, {0xD0, (uint8_t []){0x10}, 1, 0}, {0xD1, (uint8_t []){0x47}, 1, 0},
    {0xD2, (uint8_t []){0x56}, 1, 0}, {0xD3, (uint8_t []){0x65}, 1, 0}, {0xD4, (uint8_t []){0x74}, 1, 0},
    {0xD5, (uint8_t []){0x88}, 1, 0}, {0xD6, (uint8_t []){0x99}, 1, 0}, {0xD7, (uint8_t []){0x01}, 1, 0},
    {0xD8, (uint8_t []){0xBB}, 1, 0}, {0xD9, (uint8_t []){0xAA}, 1, 0}, {0xF3, (uint8_t []){0x01}, 1, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0}, {0x21, (uint8_t []){}, 0, 0}, {0x11, (uint8_t []){}, 0, 0},
    {0x00, (uint8_t []){}, 0, 120},
};

// PCB version dependent GPIO pins are now defined via macros in config.h

void EspS3Cat::InitializeI2c()
{
    i2c_master_bus_config_t i2c_bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = AUDIO_CODEC_I2C_SDA_PIN,
        .scl_io_num = AUDIO_CODEC_I2C_SCL_PIN,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags = {
            .enable_internal_pullup = 1,
        },
    };
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_cfg, &i2c_bus_));

    temperature_sensor_config_t temp_sensor_config = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 50);
    ESP_ERROR_CHECK(temperature_sensor_install(&temp_sensor_config, &temp_sensor));
    ESP_ERROR_CHECK(temperature_sensor_enable(temp_sensor));
}

// PCB version detection removed - now using SELECT_BOARD_VERSION macro in config.h

void EspS3Cat::InitializeSpi()
{
    const spi_bus_config_t bus_config = TAIJIPI_ST77916_PANEL_BUS_QSPI_CONFIG(QSPI_PIN_NUM_LCD_PCLK,
                                                                              QSPI_PIN_NUM_LCD_DATA0,
                                                                              QSPI_PIN_NUM_LCD_DATA1,
                                                                              QSPI_PIN_NUM_LCD_DATA2,
                                                                              QSPI_PIN_NUM_LCD_DATA3,
                                                                              DISPLAY_WIDTH * 8 * sizeof(uint16_t));
    ESP_ERROR_CHECK(spi_bus_initialize(QSPI_LCD_HOST, &bus_config, SPI_DMA_CH_AUTO));
}

void start_lvgl(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                int width, int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y, bool swap_xy,
                Display *display)
{
    lv_init();

#if CONFIG_SPIRAM
// lv image cache, currently only PNG is supported
    size_t psram_size_mb = esp_psram_get_size() / 1024 / 1024;
    if (psram_size_mb >= 8) {
        lv_image_cache_resize(2 * 1024 * 1024, true);
        ESP_LOGI(TAG, "Use 2MB of PSRAM for image cache");
    } else if (psram_size_mb >= 2) {
        lv_image_cache_resize(512 * 1024, true);
        ESP_LOGI(TAG, "Use 512KB of PSRAM for image cache");
    }
#endif

    ESP_LOGI(TAG, "Initializing LVGL adapter, width:%d, height:%d", width, height);
    esp_lv_adapter_config_t adapter_config = ESP_LV_ADAPTER_DEFAULT_CONFIG();
    adapter_config.task_priority = 6;
    adapter_config.task_core_id = 0;
    adapter_config.tick_period_ms = 5;
    adapter_config.task_min_delay_ms = 10;
    adapter_config.task_max_delay_ms = 100;
    adapter_config.stack_in_psram = false;
    ESP_ERROR_CHECK(esp_lv_adapter_init(&adapter_config));

    esp_lv_adapter_display_config_t display_config = ESP_LV_ADAPTER_DISPLAY_SPI_WITH_PSRAM_DEFAULT_CONFIG(
                                                         panel,
                                                         panel_io,
                                                         static_cast<uint16_t>(width),
                                                         static_cast<uint16_t>(height),
                                                         ESP_LV_ADAPTER_ROTATE_0);
    display_config.profile.use_psram = true;
    // display_config.profile.buffer_height = 20;
    display_config.profile.require_double_buffer = true;

    lv_display_t *display_ = esp_lv_adapter_register_display(&display_config);
    if (display_ == nullptr) {
        ESP_LOGE(TAG, "Failed to add display");
        return;
    }

    if (offset_x != 0 || offset_y != 0) {
        lv_display_set_offset(display_, offset_x, offset_y);
    }

    ESP_LOGI(TAG, "Starting LVGL adapter");
    esp_lv_adapter_set_dummy_draw(display_, true);
    esp_lv_adapter_start();

    esp_lv_adapter_lock(-1);
    /* Pass the display pointer directly to avoid Board::GetInstance() call */
    ui_bridge_init(display);
    esp_lv_adapter_unlock();
}

void EspS3Cat::Initializest77916Display()
{
    esp_lcd_panel_io_handle_t panel_io = nullptr;
    esp_lcd_panel_handle_t panel = nullptr;

    esp_lcd_panel_io_spi_config_t io_config = ST77916_PANEL_IO_QSPI_CONFIG(QSPI_PIN_NUM_LCD_CS, NULL, NULL);
    io_config.trans_queue_depth = 2;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)QSPI_LCD_HOST, &io_config, &panel_io));
    st77916_vendor_config_t vendor_config = {
        .init_cmds = vendor_specific_init_yysj,
        .init_cmds_size = sizeof(vendor_specific_init_yysj) / sizeof(st77916_lcd_init_cmd_t),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = QSPI_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = QSPI_LCD_BIT_PER_PIXEL,
        .flags = {
            .reset_active_high = QSPI_LCD_RST_ACTIVE_HIGH,
        },
        .vendor_config = &vendor_config,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_st77916(panel_io, &panel_config, &panel));

    esp_lcd_panel_reset(panel);
    esp_lcd_panel_init(panel);
    esp_lcd_panel_disp_on_off(panel, true);
    esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
    esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);

#if CONFIG_USE_EMOTE_MESSAGE_STYLE
    display_ = new emote::EmoteDisplay(panel, panel_io, DISPLAY_WIDTH, DISPLAY_HEIGHT);
    start_lvgl(panel_io, panel, DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY, display_);
#else
    display_ = new SpiLcdDisplay(panel_io, panel,
                                 DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
#endif
    backlight_ = new PwmBacklight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
    backlight_->RestoreBrightness();
}

void EspS3Cat::InitializeButtons()
{
    boot_button_.OnClick([this]() {
        auto &app = Application::GetInstance();
        if (app.GetDeviceState() == kDeviceStateStarting && !WifiStation::GetInstance().IsConnected()) {
            ESP_LOGI(TAG, "Boot button pressed, enter WiFi configuration mode");
            ResetWifiConfiguration();
        }
        app.ToggleChatState();
    });
    gpio_config_t power_gpio_config = {
        .pin_bit_mask = (BIT64(POWER_CTRL)),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&power_gpio_config));
    gpio_set_level(POWER_CTRL, 0);
}

void EspS3Cat::InitializePower()
{
#if SELECT_BOARD == PCB_VERSION_V1_2
    gpio_config_t power_gpio_config;
    power_gpio_config.mode = GPIO_MODE_OUTPUT;
    power_gpio_config.pin_bit_mask = BIT64(CORDEC_POWER_CTRL);
    ESP_ERROR_CHECK(gpio_config(&power_gpio_config));
    gpio_set_level(CORDEC_POWER_CTRL, 1);  // Enable codec power

    vTaskDelay(pdMS_TO_TICKS(100));
#endif
}

void EspS3Cat::InitializeCharge()
{
    charge_ = new Charge(i2c_bus_, 0x55);
    xTaskCreatePinnedToCore(Charge::TaskFunction, "batterydecTask", 3 * 1024, charge_, 6, NULL, 0);
}

void EspS3Cat::InitializeCst816sTouchPad()
{
    cst816s_touch_ = new Cst816sTouch(i2c_bus_);
    if (!cst816s_touch_->init(DISPLAY_WIDTH,
                              DISPLAY_HEIGHT,
                              DISPLAY_SWAP_XY,
                              DISPLAY_MIRROR_X,
                              DISPLAY_MIRROR_Y)) {
        ESP_LOGE(TAG, "Failed to initialize CST816S touch controller");
        delete cst816s_touch_;
        cst816s_touch_ = nullptr;
        return;
    }

    // Register touch with LVGL adapter
    // Note: This should be called after display is initialized
    lv_display_t *disp = lv_display_get_default();
    if (disp == nullptr) {
        ESP_LOGW(TAG, "Display not initialized yet, touch will be registered later");
        return;
    }

    esp_lv_adapter_touch_config_t touch_config = ESP_LV_ADAPTER_TOUCH_DEFAULT_CONFIG(disp, cst816s_touch_->get_handle());
    lv_indev_t *touch = esp_lv_adapter_register_touch(&touch_config);
    if (touch == nullptr) {
        ESP_LOGE(TAG, "Touch registration failed");
        return;
    }

    ui_bridge_attach_gesture_handler(touch);
    ESP_LOGI(TAG, "Touch registered successfully");
}

void EspS3Cat::InitializeTouchSensor()
{
    touch_sensor_ = new TouchSensor();
    if (!touch_sensor_->init()) {
        ESP_LOGE(TAG, "Failed to initialize touch sensor");
        delete touch_sensor_;
        touch_sensor_ = nullptr;
    } else {
        ESP_LOGI(TAG, "Touch sensor initialized successfully");
    }
}

EspS3Cat::EspS3Cat() : boot_button_(BOOT_BUTTON_GPIO)
{
    ESP_LOGI(TAG, "EchoEar PCB Version: %d", SELECT_BOARD);
    InitializePower();
    InitializeI2c();
    InitializeCharge();
    InitializeSpi();
    Initializest77916Display();
    InitializeButtons();
    InitializeCst816sTouchPad();
    InitializeTouchSensor();

    // Initialize modules
    base_control_ = new BaseControl(this);
    base_control_->Initialize();

    audio_analysis_ = new AudioAnalysis();
    audio_analysis_->Initialize();

    EchoEarTools::Initialize(this);
}

AudioCodec* EspS3Cat::GetAudioCodec()
{
    static BoxAudioCodec audio_codec(
        i2c_bus_,
        AUDIO_INPUT_SAMPLE_RATE,
        AUDIO_OUTPUT_SAMPLE_RATE,
        AUDIO_I2S_GPIO_MCLK,
        AUDIO_I2S_GPIO_BCLK,
        AUDIO_I2S_GPIO_WS,
        AUDIO_I2S_GPIO_DOUT,
        AUDIO_I2S_GPIO_DIN,
        AUDIO_CODEC_PA_PIN,
        AUDIO_CODEC_ES8311_ADDR,
        AUDIO_CODEC_ES7210_ADDR,
        AUDIO_INPUT_REFERENCE);
    return &audio_codec;
}

Display* EspS3Cat::GetDisplay()
{
    return display_;
}

esp_lcd_touch_handle_t EspS3Cat::GetTouchpad()
{
    return cst816s_touch_ != nullptr ? cst816s_touch_->get_handle() : nullptr;
}

Backlight* EspS3Cat::GetBacklight()
{
    return backlight_;
}

void EspS3Cat::SetAfeDataProcessCallback(std::function<void(const int16_t* audio_data, size_t total_bytes)> callback)
{
    (void)callback;  // Unused, processing is done in AudioAnalysis
    if (audio_analysis_ != nullptr) {
        audio_analysis_->SetAfeDataProcessCallback();
    }
}

void EspS3Cat::SetVadStateChangeCallback(std::function<void(bool speaking)> callback)
{
    (void)callback;  // Unused, processing is done in AudioAnalysis
    if (audio_analysis_ != nullptr) {
        audio_analysis_->SetVadStateChangeCallback();
    }
}

void EspS3Cat::SetAudioDataProcessedCallback(std::function<void(const int16_t* audio_data, size_t bytes_per_channel, size_t channels)> callback)
{
    (void)callback;  // Unused, processing is done in AudioAnalysis
    if (audio_analysis_ != nullptr) {
        audio_analysis_->SetAudioDataProcessedCallback();
    }
}

beat_detection_handle_t EspS3Cat::GetBeatDetectionHandle() const
{
    return audio_analysis_ != nullptr ? audio_analysis_->GetBeatDetectionHandle() : nullptr;
}

void EspS3Cat::SetAudioAnalysisMode(AudioAnalysisMode mode)
{
    if (audio_analysis_ != nullptr) {
        audio_analysis_->SetMode(mode);
    }
}

DECLARE_BOARD(EspS3Cat);
