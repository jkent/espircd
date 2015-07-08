# espircd
IRC server for the ESP8266 written in C

## Building instructions

*NOTE: This project assumes you already have a network configuration stored in flash --- you can do this with the AT firmware*

If you're using the esp-open-sdk, create a `Makefile.local` similar to the following:

    OPENSDK     := /path/to/esp-open-sdk
    PATH        := $(OPENSDK)/xtensa-lx106-elf/bin:$(PATH)

Otherwise you'll need to set the paths of the SDK and toolchain to use manually:

    SDK_BASE    := /path/to/esp_iot_sdk_v1.1.2
    ESPTOOL     := /path/to/esptool.py
    PATH        := /path/to/xtensa-lx106-elf/bin:$(PATH)
    USE_OPENSDK := 0

Then just run `make` or `make flash` or even `make run`.
