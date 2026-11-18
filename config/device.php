<?php

return [
    'api_key'       => env('DEVICE_API_KEY'),
    'offline_after' => 20,   // seconds without data before the device shows "offline"
    'alert_on'      => 50,   // °C - display only, keep in sync with the ESP32 sketch
    'alert_clear'   => 48,   // °C
];
