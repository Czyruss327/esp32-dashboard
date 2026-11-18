@extends('layout')
@section('title', 'Logs')
@section('content')
<div class="card">
  <h3 style="margin-top:0">Alert log</h3>
  <p class="mute">Each row is a moment the over-temperature alert started or cleared on the device.</p>
  <table>
    <thead><tr><th>Event</th><th>Temperature</th><th>Humidity</th><th>Time</th></tr></thead>
    <tbody>
    @forelse ($logs as $log)
      <tr>
        <td><span class="badge {{ $log->event }}">{{ $log->event === 'start' ? 'Alert started' : 'Alert cleared' }}</span></td>
        <td>{{ number_format($log->temperature, 1) }} °C</td>
        <td>{{ number_format($log->humidity, 1) }} %</td>
        <td>{{ $log->created_at->format('d M Y, h:i:s A') }} <span class="mute">({{ $log->created_at->diffForHumans() }})</span></td>
      </tr>
    @empty
      <tr><td colspan="4" class="mute">No alerts have been logged yet.</td></tr>
    @endforelse
    </tbody>
  </table>
  <div class="pager">
    <span>@if ($logs->previousPageUrl())<a href="{{ $logs->previousPageUrl() }}">← Newer</a>@endif</span>
    <span>@if ($logs->nextPageUrl())<a href="{{ $logs->nextPageUrl() }}">Older →</a>@endif</span>
  </div>
</div>
@endsection
