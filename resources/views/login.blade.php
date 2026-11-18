@extends('layout')
@section('title', 'Login')
@section('content')
<div class="login card">
  <h2 style="margin-top:0">ESP32 Monitor</h2>
  <p class="mute">Authorized users only.</p>
  <form method="POST" action="{{ route('login') }}">
    @csrf
    <label>Email</label>
    <input type="email" name="email" value="{{ old('email') }}" required autofocus>
    <label>Password</label>
    <input type="password" name="password" required>
    @error('email')<div class="err">{{ $message }}</div>@enderror
    <label class="mute"><input type="checkbox" name="remember"> Keep me signed in</label>
    <div style="margin-top:14px"><button style="width:100%">Log in</button></div>
  </form>
</div>
@endsection
