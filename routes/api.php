<?php

use App\Http\Controllers\Api\DeviceController;
use Illuminate\Support\Facades\Route;

// ESP32 -> server (protected by the X-API-KEY header, not by login)
Route::post('/device/reading', [DeviceController::class, 'store']);
Route::get('/device/command', [DeviceController::class, 'command']);
