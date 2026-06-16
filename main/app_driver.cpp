/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_log.h>

#include <esp_matter.h>
#include <app-common/zap-generated/attributes/Accessors.h>

#include <app_priv.h>
#include <iot_button.h>
#include <button_gpio.h>

using namespace chip::app::Clusters;
using namespace esp_matter;
using namespace esp_matter::cluster;

static const char *TAG = "app_driver";

// HARDWARE NOTE — Shelly Plus i4 input polarity.
//
// The four switch inputs are ACTIVE-HIGH: idle LOW (internal pull-down),
// closed/active drives the GPIO HIGH. This matches the RevK teardown
// ("GPIO12/14/27 ... needs configuring as pull down") and is the OPPOSITE
// of the case/back button (GPIO25), which is active-low (pull-up). An
// earlier active-low setting here left the inputs stuck and no press events
// reached the controller. iot_button with active_level = 1 enables the
// internal pull-down for us.
//
// VERIFY on your unit: watch the "Initial press" log while toggling the
// input. If presses appear inverted, flip this back to 0.
#define INPUT_ACTIVE_LEVEL 1

// Switch position values per the Matter Switch cluster.
static constexpr uint8_t kIdlePosition    = 0;
static constexpr uint8_t kPressedPosition = 1;

uint16_t app_driver_get_endpoint(gpio_button *button)
{
    return button ? button->endpoint_id : chip::kInvalidEndpointId;
}

esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val)
{
    // A Generic Switch is input-only; nothing to drive. No-op by design.
    return ESP_OK;
}

// ---------------------------------------------------------------------------
// iot_button callbacks. These run in the button task, so every Matter data
// model call is marshalled onto the Matter stack task via ScheduleLambda().
// Per-button state lives in the gpio_button passed as the callback `data`,
// keeping the four inputs independent.
// ---------------------------------------------------------------------------

// Each physical gesture maps to the Switch cluster's classic momentary event
// sequence (features MS + MomentarySwitchRelease + LongPress + MultiPress),
// which controllers such as Apple Home map to Single / Double / Long press:
//   single tap : InitialPress -> ShortRelease -> MultiPressComplete(count 1)
//   double tap : InitialPress -> ShortRelease -> MultiPressOngoing(2)
//                -> ShortRelease -> MultiPressComplete(2)
//   long press : InitialPress -> LongPress -> LongRelease
// (The previous ActionSwitch model only surfaced Single Press in Apple Home.)

static void on_press_down(void *arg, void *data)
{
    gpio_button *btn = static_cast<gpio_button *>(data);
    uint16_t ep = app_driver_get_endpoint(btn);

    // InitialPress is emitted once per gesture (the first physical press);
    // further presses of a multi-press are reported via MultiPressOngoing.
    if (btn->initial_sent) {
        return;
    }
    btn->initial_sent = true;
    btn->long_press_active = false;
    btn->press_count = 1;
    ESP_LOGI(TAG, "Initial press on endpoint %u", ep);

    chip::DeviceLayer::SystemLayer().ScheduleLambda([ep]() {
        Switch::Attributes::CurrentPosition::Set(ep, kPressedPosition);
        switch_cluster::event::send_initial_press(ep, kPressedPosition);
    });
}

static void on_press_up(void *arg, void *data)
{
    gpio_button *btn = static_cast<gpio_button *>(data);
    uint16_t ep = app_driver_get_endpoint(btn);

    if (btn->long_press_active) {
        // Release after a hold -> LongRelease, and end the gesture here. A
        // trailing PRESS_REPEAT_DONE (if any) is ignored via !initial_sent.
        ESP_LOGI(TAG, "Long release on endpoint %u", ep);
        chip::DeviceLayer::SystemLayer().ScheduleLambda([ep]() {
            Switch::Attributes::CurrentPosition::Set(ep, kIdlePosition);
            switch_cluster::event::send_long_release(ep, kPressedPosition);
        });
        btn->initial_sent = false;
        btn->long_press_active = false;
        btn->press_count = 1;
        return;
    }

    // Short release of this tap. ShortRelease fires on every quick release; the
    // gesture is finalized (and MultiPressComplete sent) in on_repeat_done().
    ESP_LOGI(TAG, "Short release on endpoint %u", ep);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([ep]() {
        Switch::Attributes::CurrentPosition::Set(ep, kIdlePosition);
        switch_cluster::event::send_short_release(ep, kPressedPosition);
    });
}

static void on_long_press(void *arg, void *data)
{
    gpio_button *btn = static_cast<gpio_button *>(data);
    uint16_t ep = app_driver_get_endpoint(btn);

    btn->long_press_active = true;
    ESP_LOGI(TAG, "Long press on endpoint %u", ep);

    chip::DeviceLayer::SystemLayer().ScheduleLambda([ep]() {
        Switch::Attributes::CurrentPosition::Set(ep, kPressedPosition);
        switch_cluster::event::send_long_press(ep, kPressedPosition);
    });
}

static void on_repeat(void *arg, void *data)
{
    // Each additional press in a multi-press gesture bumps the count and emits
    // MultiPressOngoing (part of the Release/MultiPress event model).
    gpio_button *btn = static_cast<gpio_button *>(data);
    uint16_t ep = app_driver_get_endpoint(btn);

    btn->press_count++;
    int count = btn->press_count;
    ESP_LOGI(TAG, "Multi-press ongoing on endpoint %u, count %d", ep, count);

    chip::DeviceLayer::SystemLayer().ScheduleLambda([ep, count]() {
        Switch::Attributes::CurrentPosition::Set(ep, kPressedPosition);
        switch_cluster::event::send_multi_press_ongoing(ep, kPressedPosition, count);
    });
}

static void on_repeat_done(void *arg, void *data)
{
    gpio_button *btn = static_cast<gpio_button *>(data);
    uint16_t ep = app_driver_get_endpoint(btn);

    // A hold is finalized in on_press_up() (LongRelease); ignore the trailing
    // PRESS_REPEAT_DONE so a long press is not also reported as a tap.
    if (btn->long_press_active || !btn->initial_sent) {
        return;
    }

    // Per spec, a count above MultiPressMax is reported as 0 ("more than max").
    int total = (btn->press_count > MULTIPRESS_MAX) ? 0 : btn->press_count;
    ESP_LOGI(TAG, "Multi-press complete on endpoint %u, count %d", ep, total);
    chip::DeviceLayer::SystemLayer().ScheduleLambda([ep, total]() {
        switch_cluster::event::send_multi_press_complete(ep, kPressedPosition, total);
    });

    // Reset the per-button state machine for the next gesture.
    btn->initial_sent = false;
    btn->long_press_active = false;
    btn->press_count = 1;
}

app_driver_handle_t app_driver_button_init(gpio_button *button)
{
    if (!button) {
        ESP_LOGE(TAG, "gpio_button must not be NULL");
        return NULL;
    }

    button->press_count = 1;
    button->initial_sent = false;
    button->long_press_active = false;

    button_handle_t handle = NULL;
    // long_press_time 1.5 s = a comfortable "long press" gesture. (0 would fall
    // back to CONFIG_BUTTON_LONG_PRESS_TIME_MS, which the overlay sets to 5 s.)
    const button_config_t btn_cfg = {
        .long_press_time = 1500,
        .short_press_time = 0,
    };
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = button->gpio,
        .active_level = INPUT_ACTIVE_LEVEL,
    };

    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create button device on GPIO%d", button->gpio);
        return NULL;
    }

    iot_button_register_cb(handle, BUTTON_PRESS_DOWN,        NULL, on_press_down,  button);
    iot_button_register_cb(handle, BUTTON_PRESS_UP,          NULL, on_press_up,    button);
    iot_button_register_cb(handle, BUTTON_LONG_PRESS_START,  NULL, on_long_press,  button);
    iot_button_register_cb(handle, BUTTON_PRESS_REPEAT,      NULL, on_repeat,      button);
    iot_button_register_cb(handle, BUTTON_PRESS_REPEAT_DONE, NULL, on_repeat_done, button);

    ESP_LOGI(TAG, "Input initialized on GPIO%d (active %s)",
             button->gpio, INPUT_ACTIVE_LEVEL ? "high" : "low");
    return (app_driver_handle_t)handle;
}

// ---------------------------------------------------------------------------
// Case/back button (GPIO25) -> factory reset. A ~5 s hold erases the Matter
// fabrics + Wi-Fi credentials and reboots into BLE commissioning (pairing)
// mode. The case button is active-low (idle pulled HIGH, pressed = LOW).
// ---------------------------------------------------------------------------

#define RESET_HOLD_TIME_MS 5000

static void on_reset_long_press(void *arg, void *data)
{
    // Fires once the button has been held for RESET_HOLD_TIME_MS.
    // esp_matter::factory_reset() wipes NVS and reboots; on the next boot the
    // device is uncommissioned and re-opens the BLE commissioning window.
    ESP_LOGW(TAG, "Case button held %d s -> factory reset, rebooting into pairing mode", RESET_HOLD_TIME_MS / 1000);
    esp_matter::factory_reset();
}

esp_err_t app_driver_reset_button_init(gpio_num_t gpio)
{
    button_handle_t handle = NULL;
    const button_config_t btn_cfg = {
        .long_press_time = RESET_HOLD_TIME_MS,
        .short_press_time = 0,
    };
    const button_gpio_config_t btn_gpio_cfg = {
        .gpio_num = gpio,
        .active_level = 0,  // case button is active-low (RevK: needs pull-up)
    };

    if (iot_button_new_gpio_device(&btn_cfg, &btn_gpio_cfg, &handle) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create reset button on GPIO%d", gpio);
        return ESP_FAIL;
    }

    esp_err_t err = iot_button_register_cb(handle, BUTTON_LONG_PRESS_START, NULL, on_reset_long_press, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register reset callback on GPIO%d", gpio);
        return err;
    }

    ESP_LOGI(TAG, "Factory-reset button on GPIO%d (hold %d s)", gpio, RESET_HOLD_TIME_MS / 1000);
    return ESP_OK;
}
