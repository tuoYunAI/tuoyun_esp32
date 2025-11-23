#include "wifi_configuration_bluetooth.h"
#include <cstdio>
#include <memory>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <esp_err.h>
#include <esp_event.h>
#include <esp_wifi.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <lwip/ip_addr.h>
#include <nvs.h>
#include <nvs_flash.h>
#include "ssid_manager.h"
#include "system_info.h"

#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"


#define TAG "WifiConfigurationBluetooth"


#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1


esp_err_t prov_mac_data_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                                          uint8_t **outbuf, ssize_t *outlen, void *priv_data)
{
    if (inbuf) {
        ESP_LOGI(TAG, "Received data: %.*s", inlen, (char *)inbuf);
    }
    
    std::string board_json = R"({)";
    board_json += R"("mac":")" + SystemInfo::GetMacAddress() + R"(",)";
    board_json += R"("vendor":")" + std::string(CONFIG_MANUFACTURER_CODE) + R"(")";
    board_json += R"(})";

    *outbuf = (uint8_t *)strdup(board_json.c_str());
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = board_json.length() + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}


WifiConfigurationBluetooth::WifiConfigurationBluetooth(/* args */)
{
    event_group_ = xEventGroupCreate();
}

WifiConfigurationBluetooth::~WifiConfigurationBluetooth()
{
    if (event_group_) {
        vEventGroupDelete(event_group_);
    }
    // Unregister event handlers if they were registered
    if (instance_any_id_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
    }
    if (instance_got_ip_) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
    }
}

WifiConfigurationBluetooth& WifiConfigurationBluetooth::GetInstance() {
    static WifiConfigurationBluetooth instance;
    return instance;
}



void WifiConfigurationBluetooth::Start()
{

    ESP_LOGI(TAG, "Starting Bluetooth WiFi configuration...");

    ESP_ERROR_CHECK(esp_netif_init());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );


    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_PROV_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &WifiConfigurationBluetooth::WifiProvEventHandler,
                                                        this,
                                                        &instance_any_id_));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &WifiConfigurationBluetooth::IpEventHandler,
                                                        this,
                                                        &instance_got_ip_));


                                                        
                                                      
    //ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    //ESP_ERROR_CHECK( esp_wifi_start() );


    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM
        //.app_event_handler = &WifiConfigurationBluetooth::WifiProvEventHandler
    };

    
    ESP_ERROR_CHECK( wifi_prov_mgr_init(config) );

    
    const char *service_name = "tuoyun_ai_device";
    wifi_prov_security_t security = WIFI_PROV_SECURITY_0;

    ESP_LOGI(TAG, "Starting Bluetooth WiFi configuration...");

    std::string uuid_string = CONFIG_WIFI_PROVISIONING_UUID;
    uint8_t uuid[16];
    if (0 != UuidStrToByteArray(uuid_string.c_str(), uuid)) {
        ESP_LOGE(TAG, "Invalid UUID string: %s", uuid_string.c_str());
        return;
    }

    esp_err_t ret = wifi_prov_scheme_ble_set_service_uuid(uuid);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set service UUID, %d", ret);
        return;
    }

    ESP_LOGI(TAG, "Service UUID: %s", uuid_string.c_str());
    for(int i = 0 ; i < 16; i++) {
        ESP_LOGI(TAG, "%02x", uuid[i]);
    }

    wifi_prov_mgr_endpoint_create("prov_mac");

    // 启动配网服务
    ESP_ERROR_CHECK( wifi_prov_mgr_start_provisioning(security, nullptr, service_name, nullptr) );
    wifi_prov_mgr_endpoint_register("prov_mac", prov_mac_data_handler, NULL);

    ESP_LOGI(TAG, "Waiting for Bluetooth WiFi configuration to complete...");   

};


void WifiConfigurationBluetooth::Save(const std::string &ssid, const std::string &password)
{
    ESP_LOGI(TAG, "Save SSID %s %d", ssid.c_str(), ssid.length());
    SsidManager::GetInstance().AddSsid(ssid, password);
}


void WifiConfigurationBluetooth::WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    WifiConfigurationBluetooth* self = static_cast<WifiConfigurationBluetooth*>(arg);
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t* event = (wifi_event_ap_staconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " joined, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t* event = (wifi_event_ap_stadisconnected_t*) event_data;
        ESP_LOGI(TAG, "Station " MACSTR " left, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_STA_CONNECTED) {
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(self->event_group_, WIFI_FAIL_BIT);
    } else if (event_id == WIFI_EVENT_SCAN_DONE) {
        std::lock_guard<std::mutex> lock(self->mutex_);
        uint16_t ap_num = 0;
        esp_wifi_scan_get_ap_num(&ap_num);

        self->ap_records_.resize(ap_num);
        esp_wifi_scan_get_ap_records(&ap_num, self->ap_records_.data());

        // 扫描完成，等待10秒后再次扫描
        esp_timer_start_once(self->scan_timer_, 10 * 1000000);
    }
}

void WifiConfigurationBluetooth::IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    WifiConfigurationBluetooth* self = static_cast<WifiConfigurationBluetooth*>(arg);
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(self->event_group_, WIFI_CONNECTED_BIT);
    }
}


void WifiConfigurationBluetooth::WifiProvEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    WifiConfigurationBluetooth* self = static_cast<WifiConfigurationBluetooth*>(arg);
    if (event_base == WIFI_PROV_EVENT) {
        switch (event_id) {
            case WIFI_PROV_START:
                ESP_LOGI(TAG, "Provisioning started");
                break;
            case WIFI_PROV_CRED_RECV: {
                wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;
                const char *ssid = (const char *)wifi_sta_cfg->ssid;
                const char *password = (const char *)wifi_sta_cfg->password;
                ESP_LOGI(TAG, "Received Wi-Fi credentials"
                         "\n\tSSID     : %s\n\tPassword : %s",
                         ssid,
                         password);
                self->Save(ssid, password);
                
                ESP_LOGD(TAG, "Bluetooth WiFi configuration completed");
                
                break;
            }
            case WIFI_PROV_CRED_FAIL: {
                wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
                ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s"
                         "\n\tPlease reset to factory and retry provisioning",
                         (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                         "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");
                break;
            }
            case WIFI_PROV_CRED_SUCCESS:
                ESP_LOGI(TAG, "Provisioning successful");
                break;
            case WIFI_PROV_END:
                /*配网完成后，反初始化管理器。*/
                // 最后反初始化管理器
                wifi_prov_mgr_deinit();
                self->Stop();
                xTaskCreate([](void *ctx){
                            ESP_LOGI(TAG, "Restarting in 3 second");
                            vTaskDelay(pdMS_TO_TICKS(3000));
                            esp_restart();
                        }, "restart_task", 4096, NULL, 5, NULL);
                break;
            default:
                break;
        }
    }
}

void WifiConfigurationBluetooth::Stop() {

    // 注销事件处理器
    if (instance_any_id_) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id_);
        instance_any_id_ = nullptr;
    }

    if (instance_got_ip_) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip_);
        instance_got_ip_ = nullptr;
    }

    // 停止WiFi并重置模式
    esp_wifi_stop();
    esp_wifi_deinit();
    esp_wifi_set_mode(WIFI_MODE_NULL);

    // 释放网络接口资源
    if (ap_netif_) {
        esp_netif_destroy(ap_netif_);
        ap_netif_ = nullptr;
    }

    ESP_LOGI(TAG, "Wifi configuration AP stopped");
}


int WifiConfigurationBluetooth::UuidStrToByteArray(const char *uuid_str, uint8_t *byte_array) {
    // 检查输入参数
    if (uuid_str == NULL || byte_array == NULL) {
        return -1;
    }
    
    // 检查UUID字符串长度（包括连字符）
    if (strlen(uuid_str) != 36) {
        return -1;
    }
    
    // 检查连字符位置
    if (uuid_str[8] != '-' || uuid_str[13] != '-' || 
        uuid_str[18] != '-' || uuid_str[23] != '-') {
        return -1;
    }
    
    // 提取UUID各部分并转换为字节
    const char *pos = uuid_str;
    int index = 15;
    
    // 处理UUID的5个部分
    int part_lengths[5] = {8, 4, 4, 4, 12};
    
    for (int i = 0; i < 5; i++) {
        for (int j = 0; j < part_lengths[i]; j += 2) {
            // 检查字符是否为有效的十六进制字符
            if (!isxdigit((unsigned char)pos[j]) || !isxdigit((unsigned char)pos[j+1])) {
                return -1;
            }
            
            char hex_str[3] = {pos[j], pos[j+1], '\0'};
            unsigned int value;
            sscanf(hex_str, "%x", &value);
            /**
             * esp32蓝牙配网的uuid配置结果与数组的内容顺序是相反的
             */
            byte_array[index--] = (uint8_t)value;            
        }

        // 跳过当前部分和连字符
        pos += part_lengths[i] + 1; 
    }
    
    return 0;
}