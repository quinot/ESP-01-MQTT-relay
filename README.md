ESP-01S MQTT relay
==================

This project is about various ways to control a [relay module with an ESP-01S development board](https://s.click.aliexpress.com/e/_EvE8Pl3) through MQTT.


IotWebConf
----------

This is a fork of [IotWebConf MQTT relay example](https://github.com/prampec/IotWebConf/tree/master/examples/IotWebConf07MqttRelay), tailored for my needs:
* ESP-01(S) build environment
* support for MQTT over TLS
* support for disabling Retain (needed for Scaleway IoT hub free brokers)
* added a bunch of new commands (TOGGLE, FLASH, REPORT)

Tasmota
-------

The same functionality can be obtained in a more generic way using [Tasmota](https://tasmota.github.io/docs/) with the following template:

`{"NAME":"ESP-01S Relay v1.0","GPIO":[224,1,320,1,0,0,0,0,0,0,0,0,0,0],"FLAG":0,"BASE":18}`

The `tasmota/` subdirectory contains a `user_config_override.h` enabling TLS support in MQTT.

ESPHome
-------

Next exercise: convert this to an ESPhome configuration.
