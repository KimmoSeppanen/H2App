#include "matter_bridge.h"

#include "esp_log.h"
#include "nvs_flash.h"

#include <esp_matter.h>

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#include "esp_openthread_types.h"
#include <platform/ESP32/OpenthreadLauncher.h>
#endif

static const char *TAG = "MATTER_BRIDGE";

using namespace esp_matter;

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
#define ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG() \
    { .radio_mode = RADIO_MODE_NATIVE, }

#define ESP_OPENTHREAD_DEFAULT_HOST_CONFIG() \
    { .host_connection_mode = HOST_CONNECTION_MODE_NONE, }

#define ESP_OPENTHREAD_DEFAULT_PORT_CONFIG() \
    { .storage_partition_name = "nvs", .netif_queue_size = 10, .task_queue_size = 10, }
#endif

static void matter_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    (void)arg;

    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        break;
    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;
    default:
        break;
    }
}

static esp_err_t matter_identification_cb(identification::callback_type_t type,
                                          uint16_t endpoint_id,
                                          uint8_t effect_id,
                                          uint8_t effect_variant,
                                          void *priv_data)
{
    (void)type;
    (void)endpoint_id;
    (void)effect_id;
    (void)effect_variant;
    (void)priv_data;
    return ESP_OK;
}

static esp_err_t matter_attribute_update_cb(attribute::callback_type_t type,
                                            uint16_t endpoint_id,
                                            uint32_t cluster_id,
                                            uint32_t attribute_id,
                                            esp_matter_attr_val_t *val,
                                            void *priv_data)
{
    // Phase A: Matter runtime only. Endpoint-specific logic will be added in later phases.
    (void)type;
    (void)endpoint_id;
    (void)cluster_id;
    (void)attribute_id;
    (void)val;
    (void)priv_data;
    return ESP_OK;
}

esp_err_t matter_bridge_init(void)
{
    static bool initialized = false;
    if (initialized) {
        return ESP_OK;
    }

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize NVS for Matter: %s", esp_err_to_name(err));
        return err;
    }

    node::config_t node_config;
    node_t *node = node::create(&node_config, matter_attribute_update_cb, matter_identification_cb);
    if (!node) {
        ESP_LOGE(TAG, "Failed to create Matter node");
        return ESP_FAIL;
    }

#if CHIP_DEVICE_CONFIG_ENABLE_THREAD
    esp_openthread_platform_config_t ot_platform_config = {
        .radio_config = ESP_OPENTHREAD_DEFAULT_RADIO_CONFIG(),
        .host_config = ESP_OPENTHREAD_DEFAULT_HOST_CONFIG(),
        .port_config = ESP_OPENTHREAD_DEFAULT_PORT_CONFIG(),
    };
    err = set_openthread_platform_config(&ot_platform_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set OpenThread platform config: %s", esp_err_to_name(err));
        return err;
    }
#endif

    err = esp_matter::start(matter_event_cb);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start Matter: %s", esp_err_to_name(err));
        return err;
    }

    initialized = true;
    ESP_LOGI(TAG, "Matter runtime initialized (Phase A: no application endpoint control yet)");
    return ESP_OK;
}
