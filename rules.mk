LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULE_SRCS += \
	$(LOCAL_DIR)/main.c \
	$(LOCAL_DIR)/rvk.c \
	$(LOCAL_DIR)/sm.c \
	$(LOCAL_DIR)/smsched.c

include make/module.mk
