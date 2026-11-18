<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="csrf-token" content="{{ csrf_token() }}">
<title>@yield('title', 'ESP32 Monitor')</title>
<style>
:root{color-scheme:light;--bg:#f5f7fb;--card:#fff;--text:#152238;--mute:#728096;--line:#e7ebf2;--ok:#169a69;--bad:#d64545;--accent:#3868df;--soft:#f0f4ff}
*{box-sizing:border-box}
body{margin:0;font-family:Inter,ui-sans-serif,system-ui,-apple-system,Segoe UI,Roboto,sans-serif;background:var(--bg);color:var(--text);font-size:14px}
header{display:flex;align-items:center;gap:22px;padding:14px max(22px,calc((100% - 1120px)/2));background:var(--card);border-bottom:1px solid var(--line);position:sticky;top:0;z-index:5}
header b{font-size:15px;letter-spacing:-.2px;white-space:nowrap}
nav{display:flex;gap:6px;flex:1}
nav a{padding:8px 14px;border-radius:8px;color:var(--mute);text-decoration:none;font-size:13px;font-weight:600}
nav a.active{background:var(--accent);color:#fff}
main{max-width:1120px;margin:0 auto;padding:32px 22px 48px}
.card{background:var(--card);border:1px solid var(--line);border-radius:14px;padding:20px;margin-bottom:16px;box-shadow:0 3px 14px rgba(24,44,78,.035)}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(215px,1fr));gap:14px;margin-bottom:16px}
.grid .card{margin:0}
.label{font-size:13px;color:var(--mute);margin-bottom:6px}
.big{font-size:42px;font-weight:650;letter-spacing:-1.6px;line-height:1.2;margin:12px 0 7px}.big small{font-size:17px;color:var(--mute);margin-left:5px;letter-spacing:0;font-weight:500}
.big.sm{font-size:24px}
.mute{color:var(--mute);font-size:13px}
.dot{display:inline-block;width:10px;height:10px;border-radius:50%;background:var(--mute);margin-right:6px}
.dot.on{background:var(--ok)}.dot.off{background:var(--bad)}
.status-row{margin:0 0 15px;padding:11px 14px;background:#fff;border:1px solid var(--line);border-radius:10px;font-size:13px;display:flex;align-items:center}
.leds{display:flex;gap:18px;flex-wrap:wrap}
.led{display:flex;align-items:center;gap:8px;font-size:13px;color:var(--mute)}
.led i{width:18px;height:18px;border-radius:50%;background:var(--line);border:1px solid var(--line)}
.led.lit i{box-shadow:0 0 8px currentColor}
#led1{color:#3fae49}#led2{color:#e0a100}#led3{color:#d64545}
.led.lit#led1 i{background:#3fae49}.led.lit#led2 i{background:#e0a100}.led.lit#led3 i{background:#d64545}
.alarm{border-color:var(--bad);animation:pulse 1s infinite}
.alarm .big{color:var(--bad)}
@keyframes pulse{50%{box-shadow:0 0 0 4px rgba(214,69,69,.25)}}
table{width:100%;border-collapse:collapse;font-size:14px}
th,td{text-align:left;padding:10px 8px;border-bottom:1px solid var(--line)}
th{color:var(--mute);font-weight:500}
.badge{padding:2px 10px;border-radius:999px;font-size:12px;color:#fff}
.badge.start{background:var(--bad)}.badge.clear{background:var(--ok)}
.pager{display:flex;justify-content:space-between;margin-top:14px;font-size:14px}
.pager a{color:var(--accent);text-decoration:none}
form.inline{margin:0}
button,.btn{font:inherit;cursor:pointer;border:0;border-radius:8px;padding:8px 14px;background:var(--accent);color:#fff}
button.ghost{background:transparent;color:var(--mute);border:1px solid var(--line)}
input[type=email],input[type=password]{width:100%;padding:10px;border-radius:8px;border:1px solid var(--line);background:var(--bg);color:var(--text);font:inherit;margin:4px 0 12px}
.err{color:var(--bad);font-size:13px;margin-bottom:10px}
.login{max-width:390px;margin:12vh auto}
.page-heading{display:flex;align-items:flex-end;justify-content:space-between;gap:16px;margin:0 0 24px}.page-heading h1{font-size:28px;letter-spacing:-.8px;margin:3px 0 5px}.page-heading p{margin:0}.eyebrow{font-size:10px;letter-spacing:1.4px;color:var(--accent);font-weight:750}.updated{font-size:12px;color:var(--mute);padding-bottom:3px}.pulse-dot{display:inline-block;width:7px;height:7px;background:var(--ok);border-radius:50%;margin-right:6px;box-shadow:0 0 0 3px #e5f5ee}.metric-card{min-height:151px}.metric-card .label{display:flex;align-items:center;justify-content:space-between;font-weight:600}.metric-icon{width:25px;height:25px;display:grid;place-items:center;border-radius:8px;font-weight:700}.warm{background:#fff2e9;color:#ef8736}.cool{background:#eaf2ff;color:#477ee8}.section-title{display:flex;align-items:center;justify-content:space-between;margin-bottom:18px}.section-title h2{font-size:15px;margin:0 0 3px}.section-title p{margin:0}.tag{font-size:10px;letter-spacing:.7px;padding:5px 8px;background:var(--soft);border-radius:6px;color:var(--accent);font-weight:700}#buzzer i{background:var(--line)}#buzzer.lit i{background:#7d65df;box-shadow:0 0 8px #7d65df}.card canvas{max-height:300px}.control-card{display:flex;align-items:center;justify-content:space-between;gap:24px}.control-copy{min-width:0}.control-copy .section-title{margin-bottom:14px}.control-copy strong{color:var(--text)}.press-button{flex:0 0 auto;min-width:190px;min-height:86px;border-radius:16px;background:linear-gradient(145deg,#4777ed,#3159c8);box-shadow:0 5px 0 #2447a8,0 10px 20px rgba(49,89,200,.18);display:flex;flex-direction:column;align-items:center;justify-content:center;gap:3px;font-size:11px;font-weight:750;letter-spacing:.6px;touch-action:none;user-select:none;transition:transform .12s,box-shadow .12s}.press-button small{font-size:10px;font-weight:500;letter-spacing:0;opacity:.85}.button-mark{font-size:20px;line-height:1}.press-button:hover{background:linear-gradient(145deg,#5180f0,#365fce)}.press-button:active,.press-button.holding{transform:translateY(4px);box-shadow:0 1px 0 #2447a8,0 5px 12px rgba(49,89,200,.2)}.press-button:disabled{opacity:.7;cursor:wait}
@media(max-width:600px){header{padding:12px 15px;gap:10px}header b{font-size:13px}nav{gap:0}nav a{padding:7px 9px;font-size:12px}main{padding:23px 14px 36px}.page-heading{align-items:flex-start;flex-direction:column;margin-bottom:17px}.page-heading h1{font-size:24px}.grid{grid-template-columns:repeat(2,minmax(0,1fr));gap:10px}.grid .card{padding:15px}.big{font-size:34px}.big.sm{font-size:21px}.status-row{flex-wrap:wrap;gap:2px}.updated{font-size:11px}.control-card{align-items:stretch;flex-direction:column;gap:8px}.press-button{width:100%;min-height:78px}}
</style>
</head>
<body>
@auth
<header>
  <b>ESP32 Monitor</b>
  <nav>
    <a href="{{ route('dashboard') }}" class="{{ request()->routeIs('dashboard') ? 'active' : '' }}">Dashboard</a>
    <a href="{{ route('logs') }}" class="{{ request()->routeIs('logs') ? 'active' : '' }}">Logs</a>
  </nav>
  <form class="inline" method="POST" action="{{ route('logout') }}">@csrf<button class="ghost">Log out</button></form>
</header>
@endauth
<main>@yield('content')</main>
@stack('scripts')
</body>
</html>
