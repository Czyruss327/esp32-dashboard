<?php

namespace App\Http\Controllers;

use App\Models\AlertLog;
use App\Models\DeviceCommand;
use App\Models\Reading;
use Illuminate\Http\Request;

class DashboardController extends Controller
{
    // label, description, LEDs used (LED1, LED2, LED3) - matches the ESP32 sketch
    private const MODES = [
        'IDLE'   => ['Idle',   'All outputs off',                                                  [0, 0, 0]],
        'MODE1'  => ['Mode 1', 'LED 1 + buzzer blink every 1 s',                                   [1, 0, 0]],
        'MODE2'  => ['Mode 2', 'LED 2 + buzzer blink every 0.5 s',                                 [0, 1, 0]],
        'MODE3'  => ['Mode 3', 'LED 3 + buzzer blink every 0.25 s',                                [0, 0, 1]],
        'MANUAL' => ['Manual', 'All LEDs + buzzer toggled by the button; temperature alarm armed', [1, 1, 1]],
    ];

    public function index()
    {
        return view('dashboard');
    }

    public function data()
    {
        $last   = Reading::latest('id')->first();
        $online = $last && $last->created_at->gt(now()->subSeconds(config('device.offline_after')));
        $mode   = $last ? (self::MODES[$last->mode] ?? null) : null;

        $history = Reading::latest('id')->take(60)->get()->reverse()->values()
            ->map(fn ($r) => ['t' => $r->created_at->format('H:i:s'), 'temp' => $r->temperature, 'hum' => $r->humidity]);

        $lastAlert = AlertLog::where('event', 'start')->latest('id')->first();
        $command = DeviceCommand::find(1);
        $outputsOn = $last?->mode === 'MANUAL'
            ? ($last->outputs_on ?? $command?->outputs_on ?? false)
            : false;

        return response()->json([
            'online'      => (bool) $online,
            'last_seen'   => $last?->created_at->diffForHumans(),
            'temperature' => $last?->temperature,
            'humidity'    => $last?->humidity,
            'mode_label'  => $mode[0] ?? null,
            'mode_desc'   => $mode[1] ?? null,
            'requested_mode' => $command?->mode,
            'command_version' => $command?->version ?? 0,
            'leds'        => $last?->mode === 'MANUAL'
                ? [$outputsOn, $outputsOn, $outputsOn]
                : ($mode[2] ?? [0, 0, 0]),
            'outputs_on'  => (bool) $outputsOn,
            'alert'       => (bool) ($last?->alert),
            'alert_on'    => config('device.alert_on'),
            'alert_clear' => config('device.alert_clear'),
            'last_alert'  => $lastAlert ? [
                'time'        => $lastAlert->created_at->format('d M Y, h:i:s A'),
                'temperature' => $lastAlert->temperature,
            ] : null,
            'history'     => $history,
        ]);
    }

    public function setMode(Request $request)
    {
        $action = $request->validate(['action' => 'required|in:short,long'])['action'];
        $cycleModes = ['IDLE', 'MODE1', 'MODE2', 'MODE3'];
        $command = DeviceCommand::firstOrCreate(['id' => 1], ['mode' => 'IDLE', 'outputs_on' => false, 'version' => 0]);

        if ($command->mode === 'MANUAL' && $action === 'short') {
            $command->outputs_on = !$command->outputs_on;
            $nextMode = 'MANUAL';
        } elseif ($command->mode === 'MANUAL' && $action === 'long') {
            $command->outputs_on = false;
            $nextMode = 'IDLE';
        } elseif ($action === 'long') {
            $command->outputs_on = false;
            $nextMode = 'MANUAL';
        } else {
            $current = array_search($command->mode, $cycleModes, true);
            $nextMode = $cycleModes[(($current === false ? 0 : $current) + 1) % count($cycleModes)];
            $command->outputs_on = false;
        }

        $command->mode = $nextMode;
        $command->version++;
        $command->save();

        return response()->json([
            'mode' => $nextMode,
            'version' => $command->version,
            'outputs_on' => $command->outputs_on,
            'message' => 'Mode command queued. The device applies it when it next checks in.',
        ]);
    }

    public function logs()
    {
        return view('logs', ['logs' => AlertLog::latest('id')->simplePaginate(20)]);
    }
}
