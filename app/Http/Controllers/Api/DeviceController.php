<?php

namespace App\Http\Controllers\Api;

use App\Http\Controllers\Controller;
use App\Models\AlertLog;
use App\Models\DeviceCommand;
use App\Models\Reading;
use Illuminate\Http\Request;

class DeviceController extends Controller
{
    public function command(Request $request)
    {
        $this->authorizeDevice($request);
        $command = DeviceCommand::find(1);

        return response()->json([
            'mode' => $command?->mode ?? 'IDLE',
            'version' => $command?->version ?? 0,
            'outputs_on' => $command?->outputs_on ?? false,
        ]);
    }

    // POST /api/device/reading  (sent by the ESP32)
    public function store(Request $request)
    {
        $this->authorizeDevice($request);

        $data = $request->validate([
            'temperature' => 'required|numeric|between:-40,125',
            'humidity'    => 'required|numeric|between:0,100',
            'mode'        => 'required|in:IDLE,MODE1,MODE2,MODE3,MANUAL',
            'alert'       => 'required|boolean',
            'outputs_on'  => 'sometimes|boolean',
        ]);

        $previous = Reading::latest('id')->first();
        Reading::create($data);

        // Log alert transitions with the server's clock (the ESP32 needs no real-time clock)
        $was = $previous?->alert ?? false;
        if ($data['alert'] && !$was) {
            AlertLog::create(['event' => 'start', 'temperature' => $data['temperature'], 'humidity' => $data['humidity']]);
        } elseif (!$data['alert'] && $was) {
            AlertLog::create(['event' => 'clear', 'temperature' => $data['temperature'], 'humidity' => $data['humidity']]);
        }

        // Keep the table small: occasionally delete readings older than 7 days
        if (random_int(1, 100) === 1) {
            Reading::where('created_at', '<', now()->subDays(7))->delete();
        }

        return response()->json(['ok' => true]);
    }

    private function authorizeDevice(Request $request): void
    {
        $key = (string) config('device.api_key');
        abort_if($key === '' || !hash_equals($key, (string) $request->header('X-API-KEY')), 401, 'Invalid device key');
    }
}
