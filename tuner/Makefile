# Project Name
TARGET = tuner

DHUYGENS_DIR = ../src

# Sources
CPP_SOURCES = $(wildcard *.cpp) $(wildcard $(DHUYGENS_DIR)/*.cpp)

# Library Locations
DAISYSP_DIR ?= /Users/amc/git/DaisySP
LIBDAISY_DIR ?= /Users/amc/git/libDaisy

C_INCLUDES ?=
C_INCLUDES += -I$(DHUYGENS_DIR)

CPP_STANDARD ?= -std=gnu++17
OPT ?= -O3

# APP_TYPE = BOOT_SRAM
# APP_TYPE = BOOT_QSPI

# Core location, and generic Makefile.
SYSTEM_FILES_DIR = $(LIBDAISY_DIR)/core
include $(SYSTEM_FILES_DIR)/Makefile

