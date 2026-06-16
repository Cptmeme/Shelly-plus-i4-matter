/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include <hal/gpio_types.h>

// One physical input on the Shelly Plus i4. Each maps to one Matter
// Generic Switch endpoint. The per-button press state lives here so the
// four inputs are fully independent (the official esp-matter example keeps
// this state in file-static globals, which only works for a single button).
struct gpio_button {
    gpio_num_t gpio;          // GPIO the input is wired to (I1..I4)
    uint16_t   endpoint_id;   // Matter endpoint created for this input

    // Momentary-press state machine, owned by the iot_button callbacks.
    int  press_count;         // presses counted in the current multi-press
    bool initial_sent;        // InitialPress already emitted this gesture
    bool long_press_active;   // a long press fired during this gesture
};

// Maximum presses reported in a MultiPressComplete event. Counts above
// this are reported as 0 ("more than max"), per the Switch cluster spec.
#define MULTIPRESS_MAX 5

typedef void *app_driver_handle_t;

// Look up the Matter endpoint id created for a given physical button.
// Returns the endpoint id, or 0xFFFF if the button is not registered.
uint16_t app_driver_get_endpoint(gpio_button *button);

// Initialize one physical input as a momentary push-button.
//
// Configures the GPIO via iot_button and registers the callbacks that
// translate physical presses into Matter Switch cluster events
// (InitialPress / LongPress / MultiPressComplete) on the button's
// endpoint.
//
// @param[in] button Pointer to a gpio_button (must outlive the driver;
//                   iot_button keeps the pointer for its callbacks).
//
// @return Driver handle on success, NULL on failure.
app_driver_handle_t app_driver_button_init(gpio_button *button);

// Initialize the case/back button (Shelly Plus i4: GPIO25) as a factory-reset
// control. Holding it for ~5 s erases the Matter fabrics and Wi-Fi credentials
// and reboots the device back into BLE commissioning (pairing) mode.
//
// @param[in] gpio GPIO the case button is wired to.
// @return ESP_OK on success, error otherwise.
esp_err_t app_driver_reset_button_init(gpio_num_t gpio);

// Attribute-update hook, called from the common app_attribute_update_cb().
//
// A Generic Switch is input-only — it has no writable attributes that
// drive hardware — so this is a no-op kept for structural parity with the
// esp-matter examples.
esp_err_t app_driver_attribute_update(app_driver_handle_t driver_handle, uint16_t endpoint_id, uint32_t cluster_id,
                                      uint32_t attribute_id, esp_matter_attr_val_t *val);
