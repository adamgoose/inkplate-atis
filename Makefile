# Build, flash and test from the Nix dev shell (`nix develop`).
#
#   make setup            install the Inkplate board package and library into .arduino/
#   make build            compile the firmware
#   make flash            compile and upload (PORT=/dev/cu.usbserial-XXXX to override)
#   make monitor          watch the board's serial log
#   make test             run the parser tests
#   make preview          render the live ATIS to preview.png

SKETCH := firmware/inkplate_atis
# The default 921600 baud upload is unreliable on this board.
FQBN := Inkplate_Boards:esp32:Inkplate6COLOR:UploadSpeed=115200
BOARDS_URL := https://github.com/SolderedElectronics/Dasduino-Board-Definitions-for-Arduino-IDE/raw/master/package_Dasduino_Boards_index.json
PORT ?= $(firstword $(wildcard /dev/cu.usbserial-* /dev/ttyUSB*))

# Cores and libraries live in the project instead of the home directory.
export ARDUINO_DIRECTORIES_DATA := $(CURDIR)/.arduino/data
export ARDUINO_DIRECTORIES_USER := $(CURDIR)/.arduino/user
export ARDUINO_DIRECTORIES_DOWNLOADS := $(CURDIR)/.arduino/dl
export ARDUINO_BOARD_MANAGER_ADDITIONAL_URLS := $(BOARDS_URL)

ARDUINO := arduino-cli
BUILD := .arduino/build

.PHONY: setup build flash monitor test preview clean

setup:
	$(ARDUINO) core update-index
	$(ARDUINO) core install Inkplate_Boards:esp32@8.1.0
	$(ARDUINO) lib install InkplateLibrary@11.2.1
	@# The bundled esptool is an x86_64 binary; use the one on PATH instead.
	@dir="$$(ls -d .arduino/data/packages/Inkplate_Boards/tools/esptool_py/*)"; \
	  [ -e "$$dir/esptool.bundled" ] || mv "$$dir/esptool" "$$dir/esptool.bundled"; \
	  printf '#!/bin/sh\nexec esptool "$$@"\n' > "$$dir/esptool"; chmod +x "$$dir/esptool"

$(SKETCH)/config.h:
	@echo "Copy $(SKETCH)/config.example.h to $(SKETCH)/config.h and set your WiFi credentials." && false

build: $(SKETCH)/config.h
	$(ARDUINO) compile --fqbn $(FQBN) --build-path $(BUILD) $(SKETCH)

flash: build
	@[ -n "$(PORT)" ] || { echo "No serial port found; pass PORT=/dev/..."; false; }
	$(ARDUINO) upload --fqbn $(FQBN) --input-dir $(BUILD) -p $(PORT) $(SKETCH)

monitor:
	@[ -n "$(PORT)" ] || { echo "No serial port found; pass PORT=/dev/..."; false; }
	$(ARDUINO) monitor -p $(PORT) -c baudrate=115200

test preview:
	$(MAKE) -C sim $@

clean:
	$(MAKE) -C sim clean
	$(ARDUINO) cache clean
