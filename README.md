# ESP32 Monitor

A Laravel dashboard for monitoring an ESP32 environmental sensor. The web app shows connection status, the latest temperature and humidity, operating mode, output indicators, a 60-reading chart, and alert history.

## Run locally

Requirements: PHP 8.2+, Composer, Node.js/npm, and the PHP SQLite extension.

```powershell
cd esp32_dashboard
Copy-Item .env.example .env
composer install
php artisan key:generate
```

Set these values in `.env` before continuing. Use a long random device key and choose a private admin password:

```dotenv
APP_NAME="ESP32 Monitor"
APP_URL=http://127.0.0.1:8000
APP_TIMEZONE=Asia/Manila
DB_CONNECTION=sqlite
DB_DATABASE=database/database.sqlite
SESSION_DRIVER=database
CACHE_STORE=database
QUEUE_CONNECTION=database
DEVICE_API_KEY=replace-with-a-long-random-secret
ADMIN_EMAIL=admin@example.com
ADMIN_PASSWORD=replace-with-a-private-password
```

Then create the database, apply migrations, create the dashboard login, and start Laravel:

```powershell
if (!(Test-Path database/database.sqlite)) { New-Item database/database.sqlite -ItemType File }
php artisan migrate --force
php artisan db:seed --force
php artisan serve --host=0.0.0.0
```

Open [http://127.0.0.1:8000](http://127.0.0.1:8000) and sign in with `ADMIN_EMAIL` and `ADMIN_PASSWORD`. Keep the device key and admin password private. Do not commit `.env`.

## Send readings from the device

The ESP32 posts JSON to `POST /api/device/reading` every few seconds. It must send the same `DEVICE_API_KEY` value in the `X-API-KEY` request header.

```http
POST /api/device/reading
Content-Type: application/json
X-API-KEY: your-device-key
```

```json
{
  "temperature": 24.6,
  "humidity": 58.2,
  "mode": "IDLE",
  "alert": false
}
```

`mode` must be one of `IDLE`, `MODE1`, `MODE2`, `MODE3`, or `MANUAL`. Temperature is accepted from -40 to 125 °C and humidity from 0 to 100%. Successful requests return `{"ok":true}`. The dashboard considers readings older than 20 seconds offline. The alert threshold labels are display settings in `config/device.php`; keep them in sync with the device firmware.

The dashboard polls every three seconds. The ESP32 firmware polls dashboard commands every second. Its history chart loads Chart.js from jsDelivr, so that chart needs an internet connection; the current readings and status do not.

## Dashboard mode button

On the dashboard, a short press cycles `IDLE → MODE1 → MODE2 → MODE3 → IDLE`. A three-second hold enters `MANUAL`; a second three-second hold exits to `IDLE`. Entering or leaving Manual should produce a double-beep confirmation. In Manual, each short press toggles all three LEDs and the buzzer together. The three automatic blink modes are inactive while in Manual. The request is stored on the server and authenticated with the admin session.

For the ESP32 to apply dashboard mode requests, its firmware must poll `GET /api/device/command` with the same `X-API-KEY` header used for readings. The response contains the desired mode, manual output state, and a version number, for example `{"mode":"MANUAL","outputs_on":true,"version":2}`. Apply changed commands when the version changes; when entering or leaving Manual, play the double-beep confirmation. In Manual, use `outputs_on` to set all LEDs and the buzzer together, and do not run the automatic blink patterns. Continue posting readings with the actual `mode` and include `outputs_on` while in Manual so the dashboard reflects the physical toggle state. The PlatformIO project is in the repository root, with firmware in `hardware/src/`. Open the repository root in PlatformIO, edit `src/secrets.h` with your 2.4 GHz Wi-Fi name/password and the computer�s LAN address for the Laravel server (for example, `http://192.168.1.50:8000`). The device key is already matched to the local `.env`; if you change it, keep both values identical. Run Laravel with `php artisan serve --host=0.0.0.0` so the ESP32 can connect over Wi-Fi, then build and upload. The firmware polls the command endpoint every second and applies dashboard mode requests.
