#pragma once

#include <esp_err.h>

typedef enum {
    LED_STATE_OFF,                  // LED off (idle, not advertising)
    LED_STATE_BLE_ADVERTISING,      // Rapid blink — uncommissioned, advertising
    LED_STATE_WIFI_CONNECTING,      // Slow blink — joining Wi-Fi / awaiting IP
    LED_STATE_WIFI_CONNECTED,       // Solid on — fully operational
    LED_STATE_FAULT,                // Very rapid blink — error condition
    LED_STATE_IDENTIFY,             // Asymmetric blink — Matter Identify command

} led_state_t;

// Initialize the status LED system. Call once during boot.
esp_err_t status_led_init(void);

// Change the LED state. Safe to call from any task.
void status_led_set_state(led_state_t state);

// Enter Identify mode. Saves current state for later restoration.
void status_led_start_identify(void);

// Exit Identify mode. Restores previous state.
void status_led_stop_identify(void);