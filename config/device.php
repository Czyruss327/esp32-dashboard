<?php

return [
    'api_key'       => env('DEVICE_API_KEY'),
    'offline_after' => 20,   // seconds without data before the device shows "offline"
    'alert_on'      => 40,   // °C - keep in sync with the ESP32 firmware
    'alert_clear'   => 37,   // °C
];
