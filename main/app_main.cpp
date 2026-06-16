/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_err.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <esp_matter.h>
#include <esp_matter_console.h>
#include <esp_matter_ota.h>

#include <common_macros.h>
#include <log_heap_numbers.h>

#include <app_priv.h>
#include <status_led.h>

#include <app/server/CommissioningWindowManager.h>
#include <app/server/Server.h>

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
#include <esp_matter_providers.h>
#include <lib/support/Span.h>
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
#include <platform/ESP32/ESP32SecureCertDACProvider.h>
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
#include <platform/ESP32/ESP32FactoryDataProvider.h>
#endif
using namespace chip::DeviceLayer;
#endif

static const char *TAG = "app_main";

using namespace esp_matter;
using namespace esp_matter::attribute;
using namespace esp_matter::endpoint;
using namespace esp_matter::cluster;
using namespace chip::app::Clusters;

constexpr auto k_timeout_seconds = 300;

// --- Shelly Plus i4 input map ----------------------------------------------
// Verified input GPIOs from the RevK teardown and the ESPHome device DB
// (https://devices.esphome.io/devices/shelly-plus-i4), cross-checked against
// THIS unit's flash-pad eFuses. The earlier {8,10,23,22} map was wrong and
// crashed at boot: GPIO8 is the embedded flash's D1 data line
// (eFuse SPI_PAD_CONFIG_D = 8), and GPIO16/17 carry the remapped flash CS/D0 -
// none of those can be used as I/O. The status LED on GPIO0 is correct.
//
// NOTE: I1 is GPIO12 (MTDI) - a strapping pin that selects flash voltage at
// reset; stock hardware holds it low at boot, so input use after boot is fine.
static gpio_button s_inputs[] = {
    { .gpio = GPIO_NUM_12 },  // I1
    { .gpio = GPIO_NUM_14 },  // I2
    { .gpio = GPIO_NUM_27 },  // I3
    { .gpio = GPIO_NUM_26 },  // I4
};
static constexpr int kNumInputs = sizeof(s_inputs) / sizeof(s_inputs[0]);

// Case/back button on the Shelly Plus i4 enclosure (RevK teardown). Held ~5 s
// it factory-resets Matter and reboots into pairing mode.
static constexpr gpio_num_t kResetButtonGpio = GPIO_NUM_25;

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
extern const uint8_t cd_start[] asm("_binary_certification_declaration_der_start");
extern const uint8_t cd_end[] asm("_binary_certification_declaration_der_end");

const chip::ByteSpan cdSpan(cd_start, static_cast<size_t>(cd_end - cd_start));
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

#if CONFIG_ENABLE_ENCRYPTED_OTA
extern const char decryption_key_start[] asm("_binary_esp_image_encryption_key_pem_start");
extern const char decryption_key_end[] asm("_binary_esp_image_encryption_key_pem_end");

static const char *s_decryption_key = decryption_key_start;
static const uint16_t s_decryption_key_len = decryption_key_end - decryption_key_start;
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

static void app_event_cb(const ChipDeviceEvent *event, intptr_t arg)
{
    switch (event->Type) {
    case chip::DeviceLayer::DeviceEventType::kInterfaceIpAddressChanged:
        // Wi-Fi station obtained (or refreshed) an IP address. For a
        // commissioned device this is our "operational / connected" signal.
        ESP_LOGI(TAG, "Interface IP Address changed");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() > 0) {
            status_led_set_state(LED_STATE_WIFI_CONNECTED);
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningComplete:
        ESP_LOGI(TAG, "Commissioning complete");
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning complete");
        // Joined a fabric; we are (or are about to be) on Wi-Fi.
        status_led_set_state(LED_STATE_WIFI_CONNECTING);
        break;

    case chip::DeviceLayer::DeviceEventType::kFailSafeTimerExpired:
        ESP_LOGI(TAG, "Commissioning failed, fail safe timer expired");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStarted:
        ESP_LOGI(TAG, "Commissioning session started");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningSessionStopped:
        ESP_LOGI(TAG, "Commissioning session stopped");
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowOpened:
        ESP_LOGI(TAG, "Commissioning window opened");
        MEMORY_PROFILER_DUMP_HEAP_STAT("commissioning window opened");
        // Only show the BLE-advertising pattern if the device is truly
        // uncommissioned. A commissioned device may open a window for
        // multi-admin pairing; keep its current state in that case.
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            status_led_set_state(LED_STATE_BLE_ADVERTISING);
        } else {
            ESP_LOGI(TAG, "Device already commissioned, keeping current LED state");
        }
        break;

    case chip::DeviceLayer::DeviceEventType::kCommissioningWindowClosed:
        ESP_LOGI(TAG, "Commissioning window closed");
        break;


        
    case chip::DeviceLayer::DeviceEventType::kFabricRemoved: {
        ESP_LOGI(TAG, "Fabric removed successfully");
        if (chip::Server::GetInstance().GetFabricTable().FabricCount() == 0) {
            chip::CommissioningWindowManager &commissionMgr = chip::Server::GetInstance().GetCommissioningWindowManager();
            constexpr auto kTimeoutSeconds = chip::System::Clock::Seconds16(k_timeout_seconds);
            if (!commissionMgr.IsCommissioningWindowOpen()) {
                // After removing the last fabric, re-open the commissioning
                // window via DNS-SD so the device can be re-adopted.
                CHIP_ERROR err = commissionMgr.OpenBasicCommissioningWindow(kTimeoutSeconds,
                                                                            chip::CommissioningWindowAdvertisement::kDnssdOnly);
                if (err != CHIP_NO_ERROR) {
                    ESP_LOGE(TAG, "Failed to open commissioning window, err:%" CHIP_ERROR_FORMAT, err.Format());
                }
            }
        }
        break;
    }

    case chip::DeviceLayer::DeviceEventType::kBLEDeinitialized:
        ESP_LOGI(TAG, "BLE deinitialized and memory reclaimed");
        MEMORY_PROFILER_DUMP_HEAP_STAT("BLE deinitialized");
        break;

    default:
        break;
    }
}

// Invoked when a controller (Apple Home, HA, Google Home) issues an Identify
// command. We blink the status LED so the user can tell which device is which.
static esp_err_t app_identification_cb(identification::callback_type_t type, uint16_t endpoint_id, uint8_t effect_id,
                                       uint8_t effect_variant, void *priv_data)
{
    ESP_LOGI(TAG, "Identification callback: type: %u, effect: %u, variant: %u", type, effect_id, effect_variant);

    switch (type) {
    case identification::callback_type_t::START:
    case identification::callback_type_t::EFFECT:
        status_led_start_identify();
        break;
    case identification::callback_type_t::STOP:
        status_led_stop_identify();
        break;
    default:
        ESP_LOGW(TAG, "Unknown identification callback type: %u", type);
        break;
    }
    return ESP_OK;
}

// Called for every attribute update. A Generic Switch has no driver-backed
// attributes, so this just forwards to the (no-op) driver hook.
static esp_err_t app_attribute_update_cb(attribute::callback_type_t type, uint16_t endpoint_id, uint32_t cluster_id,
                                         uint32_t attribute_id, esp_matter_attr_val_t *val, void *priv_data)
{
    esp_err_t err = ESP_OK;
    if (type == PRE_UPDATE) {
        app_driver_handle_t driver_handle = (app_driver_handle_t)priv_data;
        err = app_driver_attribute_update(driver_handle, endpoint_id, cluster_id, attribute_id, val);
    }
    return err;
}

// Create one Generic Switch endpoint for a physical input, configured as a
// momentary push-button that reports short / long / multi presses.
static esp_err_t create_switch_endpoint(gpio_button *button, node_t *node, int index)
{
    // Wire up the GPIO and its press->event callbacks first; the returned
    // handle becomes the endpoint's priv_data.
    app_driver_handle_t handle = app_driver_button_init(button);
    ABORT_APP_ON_FAILURE(handle != nullptr, ESP_LOGE(TAG, "Failed to init input I%d", index + 1));

    // Momentary switch is selected at creation via the feature flags.
    generic_switch::config_t switch_config;
    switch_config.switch_cluster.feature_flags = switch_cluster::feature::momentary_switch::get_id();

    endpoint_t *endpoint = generic_switch::create(node, &switch_config, ENDPOINT_FLAG_NONE, handle);
    ABORT_APP_ON_FAILURE(endpoint != nullptr, ESP_LOGE(TAG, "Failed to create generic switch endpoint"));

    button->endpoint_id = endpoint::get_id(endpoint);

    // Layer the remaining momentary features onto the Switch cluster using the
    // classic Release-based model (NOT ActionSwitch). ActionSwitch and
    // MomentarySwitchRelease are mutually exclusive; the Release model is what
    // Apple Home maps to Single / Double / Long press (ActionSwitch surfaced
    // only Single Press). Dependency order: MomentarySwitchRelease (needs MS,
    // !ActionSwitch) -> LongPress (needs MS + Release) -> MultiPress.
    cluster_t *cluster = cluster::get(endpoint, Switch::Id);
    switch_cluster::feature::momentary_switch_release::add(cluster);
    switch_cluster::feature::momentary_switch_long_press::add(cluster);
    switch_cluster::feature::momentary_switch_multi_press::config_t msm;
    msm.multi_press_max = MULTIPRESS_MAX;
    switch_cluster::feature::momentary_switch_multi_press::add(cluster, &msm);

    ESP_LOGI(TAG, "Generic Switch I%d on GPIO%d -> endpoint %u", index + 1, button->gpio, button->endpoint_id);
    return ESP_OK;
}

extern "C" void app_main()
{
    esp_err_t err = ESP_OK;

    // Initialize the ESP NVS layer
    nvs_flash_init();

    // Initialize status LED
    status_led_init();

    MEMORY_PROFILER_DUMP_HEAP_STAT("Bootup");

    // Create a Matter node and add the mandatory Root Node device type on endpoint 0
    node::config_t node_config;
    node_t *node = node::create(&node_config, app_attribute_update_cb, app_identification_cb);
    ABORT_APP_ON_FAILURE(node != nullptr, ESP_LOGE(TAG, "Failed to create Matter node"));

    MEMORY_PROFILER_DUMP_HEAP_STAT("node created");

    // One Generic Switch endpoint per physical input (I1..I4)
    for (int i = 0; i < kNumInputs; i++) {
        err = create_switch_endpoint(&s_inputs[i], node, i);
        ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to create switch endpoint I%d", i + 1));
    }

    // Case/back button: hold ~5 s to factory-reset and re-enter pairing mode.
    err = app_driver_reset_button_init(kResetButtonGpio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init factory-reset button (err %d)", err);
    }

#ifdef CONFIG_ENABLE_SET_CERT_DECLARATION_API
    auto *dac_provider = get_dac_provider();
#ifdef CONFIG_SEC_CERT_DAC_PROVIDER
    static_cast<ESP32SecureCertDACProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#elif defined(CONFIG_FACTORY_PARTITION_DAC_PROVIDER)
    static_cast<ESP32FactoryDataProvider *>(dac_provider)->SetCertificationDeclaration(cdSpan);
#endif
#endif // CONFIG_ENABLE_SET_CERT_DECLARATION_API

    // Matter start
    err = esp_matter::start(app_event_cb);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to start Matter, err:%d", err));

    MEMORY_PROFILER_DUMP_HEAP_STAT("matter started");

#if CONFIG_ENABLE_ENCRYPTED_OTA
    err = esp_matter_ota_requestor_encrypted_init(s_decryption_key, s_decryption_key_len);
    ABORT_APP_ON_FAILURE(err == ESP_OK, ESP_LOGE(TAG, "Failed to initialized the encrypted OTA, err: %d", err));
#endif // CONFIG_ENABLE_ENCRYPTED_OTA

#if CONFIG_ENABLE_CHIP_SHELL
    esp_matter::console::diagnostics_register_commands();
    esp_matter::console::wifi_register_commands();
    esp_matter::console::factoryreset_register_commands();
    esp_matter::console::attribute_register_commands();
    esp_matter::console::init();
#endif

    while (true) {
        MEMORY_PROFILER_DUMP_HEAP_STAT("Idle");
        vTaskDelay(10000 / portTICK_PERIOD_MS);
    }
}
