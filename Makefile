# Developer conveniences. None of this is required to flash the board -
# see README.md for the Arduino IDE route.
#
#   make test           run every host-side test (firmware + tools)
#   make test-firmware  C++ codec, protocol and read-only build tests
#   make test-tools     Python tests for tools/candiff.py and udsdecode.py
#   make build          compile the firmware with arduino-cli
#   make upload         compile and flash (PORT=/dev/ttyUSB0)
#   make clean

CXX      ?= g++
CXXFLAGS ?= -std=c++11 -O2 -Wall -Wextra -Wpedantic -Wshadow -Wconversion \
            -Wsign-conversion -Werror

FQBN ?= arduino:avr:uno
PORT ?= /dev/ttyUSB0

BUILD_DIR := build
SKETCH    := slcan_firmware_uno

.PHONY: all test test-firmware test-tools build upload clean

all: test

# ------------------------------------------------------------------ #
# Host-side unit tests                                                #
# ------------------------------------------------------------------ #

# The codec is pure C: compiled and tested on its own.
$(BUILD_DIR)/test_slcan_codec: test/test_slcan_codec.cpp \
                               $(SKETCH)/slcan_codec.cpp \
                               $(SKETCH)/slcan_codec.h
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -o $@ test/test_slcan_codec.cpp $(SKETCH)/slcan_codec.cpp

# The command interpreter and the MCP2515 wrapper are compiled against a
# mock HAL (test/mocks) so the real firmware logic - not a copy of it -
# can be exercised on a development machine.
PROTO_SRC := test/test_slcan_protocol.cpp test/mocks/mock_hal.cpp \
             $(SKETCH)/slcan_protocol.cpp $(SKETCH)/can_iface.cpp \
             $(SKETCH)/slcan_codec.cpp

$(BUILD_DIR)/test_slcan_protocol: $(PROTO_SRC) \
                                  $(wildcard test/mocks/*.h) \
                                  $(wildcard $(SKETCH)/*.h)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(SKETCH) -Itest/mocks -o $@ $(PROTO_SRC)

# The same firmware sources, rebuilt with the safety interlock enabled.
RO_SRC := test/test_readonly_build.cpp test/mocks/mock_hal.cpp \
          $(SKETCH)/slcan_protocol.cpp $(SKETCH)/can_iface.cpp \
          $(SKETCH)/slcan_codec.cpp

$(BUILD_DIR)/test_readonly_build: $(RO_SRC) \
                                  $(wildcard test/mocks/*.h) \
                                  $(wildcard $(SKETCH)/*.h)
	@mkdir -p $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -DSLCAN_READ_ONLY=1 -I$(SKETCH) -Itest/mocks -o $@ $(RO_SRC)

test: test-firmware test-tools

test-firmware: $(BUILD_DIR)/test_slcan_codec \
               $(BUILD_DIR)/test_slcan_protocol \
               $(BUILD_DIR)/test_readonly_build
	@./$(BUILD_DIR)/test_slcan_codec
	@echo
	@./$(BUILD_DIR)/test_slcan_protocol
	@echo
	@./$(BUILD_DIR)/test_readonly_build

# ------------------------------------------------------------------ #
# Host-side analysis tools                                            #
# ------------------------------------------------------------------ #

PYTHON ?= python3

test-tools:
	@echo
	@$(PYTHON) test/test_candiff.py
	@$(PYTHON) test/test_udsdecode.py

# ------------------------------------------------------------------ #
# Firmware                                                            #
# ------------------------------------------------------------------ #

build:
	arduino-cli compile --fqbn $(FQBN) --warnings all $(SKETCH)

upload:
	arduino-cli compile --fqbn $(FQBN) --upload --port $(PORT) $(SKETCH)

clean:
	rm -rf $(BUILD_DIR)
