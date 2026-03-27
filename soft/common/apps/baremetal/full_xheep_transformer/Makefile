APPNAME := full_xheep_transformer
APP_PATH := $(dir $(abspath $(lastword $(MAKEFILE_LIST))))
CFLAGS += -I$(APP_PATH)data_cpp -I$(APP_PATH)SYLT-FFT -I$(APP_PATH)
CFLAGS += -I$(APP_PATH)common -I$(APP_PATH)include
include $(DRIVERS)/common_bare.mk
