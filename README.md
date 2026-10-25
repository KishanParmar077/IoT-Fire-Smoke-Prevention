# IoT Fire and Smoke Prevention System

An ESP32-based fire and smoke monitoring project using an MQ-2 smoke sensor, flame sensor, DHT22 temperature/humidity sensor, LEDs, buzzer, relay-controlled pump, and Blynk IoT dashboard.

## Features

- Detects a flame using a digital flame sensor.
- Reads smoke level from the MQ-2 sensor and learns a clean-air baseline after warm-up.
- Measures temperature and humidity with a DHT22.
- Estimates an **AI-style fire-risk score (0–100%)** from sensor readings. This is a rule-based score, not a trained machine-learning model.
- Turns on the red LED, buzzer, and pump when the fire state is confirmed.
- Turns off the pump and restores the normal indicators after the flame clears and the recovery period passes.
- Sends readings and fire status to Blynk when internet connectivity is available.
- Keeps local sensor monitoring and output control running when Wi-Fi/Blynk is disconnected.

## Hardware

- ESP32 development board
- MQ-2 smoke/gas sensor
- Digital flame sensor module
- DHT22 temperature and humidity sensor
- Red LED and green LED (use suitable current-limiting resistors)
- Buzzer module
- Relay module suitable for the pump's voltage/current
- Pump, external power supply, jumper wires

## Pin mapping

| Component | ESP32 pin |
|---|---:|
| MQ-2 analog output | GPIO 34 |
| Flame sensor digital output | GPIO 27 |
| DHT22 data | GPIO 26 |
| Red LED | GPIO 14 |
| Green LED | GPIO 18 |
| Buzzer | GPIO 13 |
| Relay input | GPIO 25 |

## Blynk virtual pins

| Virtual pin | Data |
|---|---|
| V0 | Temperature (°C) |
| V1 | Humidity (%) |
| V2 | Relative smoke reading |
| V3 | Flame confirmed (0/1) |
| V4 | Fire state (0/1) |
| V5 | Pump state (0/1; app manual control disabled by default) |
| V7 | Estimated fire-risk score (%) |

The event code used for notifications is `iot_fire_and_smoke_prevention`. Create a matching event in your Blynk template and enable event notifications if you want push alerts.

## Setup

1. Install Arduino IDE and the ESP32 board package.
2. Install these Arduino libraries:
   - **Blynk** (Blynk IoT)
   - **DHT sensor library** by Adafruit
   - **Adafruit Unified Sensor** (dependency for the DHT library)
3. Open `firmware/IoT_Fire_Smoke_Prevention.ino`.
4. Replace `YOUR_BLYNK_AUTH_TOKEN`, `YOUR_WIFI_SSID`, and `YOUR_WIFI_PASSWORD` with your own credentials. Do not commit real credentials to a public repository.
5. Confirm the sensor module logic levels and wiring for your specific modules.
6. Select the correct ESP32 board and COM port in Arduino IDE, then upload.
7. Open Serial Monitor at **115200 baud**.
8. In Blynk, configure the datastreams listed above and the event code.

## Important configuration notes

- The sketch is configured for a **DHT22**. If your hardware is DHT11, change `#define DHT_TYPE DHT22` to `DHT11`.
- `FLAME_ACTIVE_LEVEL` is set to `LOW`, as is common for many digital flame modules. Verify your module's output.
- `RELAY_ACTIVE_LOW` is set to `1` (LOW turns the relay on). Verify your relay module before connecting a pump.
- `BUZZER_ACTIVE_HIGH` and `LED_ACTIVE_HIGH` are set to `0`, so the code assumes active-low buzzer/LED circuits. Change only if your wiring requires it.
- `EXTRA_RULES` and `AI_TRIGGER` are disabled by default. With these defaults, the fire state is triggered by the confirmed flame sensor; smoke, temperature and risk are monitored and reported but do not independently activate the pump.
- MQ-2 warm-up and baseline learning take about **70 seconds** total.
- The recovery delay is 5 seconds after the flame signal clears.
- The code's “AI risk” is a heuristic score; it is not a certified fire detector and should not be relied upon as the only fire-safety system.

## Relay and pump safety

- Test the relay with the pump disconnected first.
- Use the relay module's rated voltage/current and an appropriate external supply for the pump.
- Connect the pump's switched supply through **COM and NO** if you want it normally off. Never power a pump directly from an ESP32 GPIO.
- Keep grounds common where required by the module's design.
- Some relay modules may not reliably recognize the ESP32's 3.3 V HIGH level. Use a suitable transistor/level-shifter interface if required.
- Mains voltage can cause fatal injury. Do not wire mains-powered equipment unless qualified; use a properly enclosed, rated interface and qualified assistance.
- This is an educational prototype, not a replacement for certified smoke alarms, fire suppression equipment, or emergency procedures.

## Serial test commands

| Key | Action |
|---|---|
| `s` | Print relay settings and pin level |
| `l` | Toggle relay active-low/active-high mode (test only; resets on reboot) |
| `1` | Manual pump test; disabled by default |
| `0` | Clear manual pump request |

## Repository structure

```text
iot-fire-smoke-prevention/
├── README.md
├── .gitignore
├── LICENSE
├── docs/
│   └── wiring-and-blynk.md
└── firmware/
    └── IoT_Fire_Smoke_Prevention.ino
```

## License

This project is released under the MIT License. See `LICENSE`.
