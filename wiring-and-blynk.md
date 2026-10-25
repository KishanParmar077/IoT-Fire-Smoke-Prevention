# Wiring and Blynk Setup

## Wiring checklist

- MQ-2 AO → GPIO34; sensor GND → ESP32 GND. Check AO voltage: ESP32 pins are not 5 V tolerant.
- Flame sensor DO → GPIO27; configure the code's active level to match your module.
- DHT22 DATA → GPIO26. Use the appropriate pull-up resistor if your sensor/module requires one.
- Red LED → GPIO14 through a current-limiting resistor.
- Green LED → GPIO18 through a current-limiting resistor.
- Buzzer input → GPIO13, if the buzzer module can be driven safely by the ESP32. Otherwise use a transistor driver.
- Relay IN → GPIO25. Relay power and pump supply must follow the relay module and pump ratings.
- Ensure all required low-voltage grounds are connected together.

## Blynk datastreams

Create the following virtual-pin datastreams in your Blynk template:

| Pin | Suggested data type | Units / meaning |
|---|---|---|
| V0 | Double | Temperature, °C |
| V1 | Double | Humidity, % |
| V2 | Integer | Relative smoke reading |
| V3 | Integer | Flame confirmed, 0 or 1 |
| V4 | Integer | Fire state, 0 or 1 |
| V5 | Integer | Pump state, 0 or 1 |
| V7 | Integer | Estimated fire risk, 0–100% |

Create a Blynk event with code `iot_fire_and_smoke_prevention` for push notifications. Event code is case-sensitive.

## First power-on test

1. Disconnect the pump from the relay contacts.
2. Power the ESP32 and confirm the relay stays off during startup.
3. Check the Serial Monitor readings and verify the flame input changes as expected.
4. Confirm the relay activates only when the intended fire condition is detected.
5. Only after low-voltage tests pass, connect the pump supply using a properly rated relay and safe enclosure.
