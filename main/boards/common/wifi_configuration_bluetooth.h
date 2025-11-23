#ifndef WIFI_CONFIGURATION_BLUETOOTH_H
#define WIFI_CONFIGURATION_BLUETOOTH_H


#include <string>
#include <vector>
#include <mutex>

#include <esp_event.h>
#include <esp_timer.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>


class WifiConfigurationBluetooth
{
private:

    /* data */
    std::mutex mutex_;
    EventGroupHandle_t event_group_;
    std::string ssid_prefix_;
    std::string language_;
    esp_event_handler_instance_t instance_any_id_;
    esp_event_handler_instance_t instance_got_ip_;
    esp_timer_handle_t scan_timer_ = nullptr;
    bool is_connecting_ = false;
    esp_netif_t* ap_netif_ = nullptr;
    std::vector<wifi_ap_record_t> ap_records_;


public:
    static WifiConfigurationBluetooth& GetInstance();
    void Start();
    void Stop();

private:
    WifiConfigurationBluetooth();
    ~WifiConfigurationBluetooth();

    void Save(const std::string &ssid, const std::string &password);

    int UuidStrToByteArray(const char *uuid_str, uint8_t *byte_array);

    // Event handlers
    static void WifiEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void IpEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
    static void WifiProvEventHandler(void* arg, const char* event_base, int32_t event_id, void* event_data);
    esp_event_handler_instance_t sc_event_instance_ = nullptr;    
    
};




#endif