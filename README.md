# esp32_mqtt_sensor

ESP-IDF project for ESP32-C6 that measures a battery voltage via ADC and reports it to an MQTT broker periodically.

Quick start

1. Set target to esp32c6:

```
idf.py set-target esp32c6
```

2. Configure WiFi, MQTT and ADC options:

```
idf.py menuconfig
```

Open "ESP32 MQTT Sensor configuration" and set `WIFI_SSID`, `WIFI_PASSWORD`, `MQTT_URI`, and any divider/resolution values.

3. Build and flash:

```
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Notes
- The Kconfig option `BAT_ADC_CHANNEL` expects the ADC1 channel number (0..7) corresponding to `ADC1_CHANNEL_0`..`ADC1_CHANNEL_7`.
- Adjust `BATTERY_DIVIDER_R1` and `BATTERY_DIVIDER_R2` to match your hardware voltage divider.
- The project publishes a JSON payload like `{"voltage":3.712,"mv":3712,"percent":78}` to the configured MQTT topic.
