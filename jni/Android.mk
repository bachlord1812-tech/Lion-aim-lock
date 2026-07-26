LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)
LOCAL_MODULE := LionAimLock
LOCAL_SRC_FILES := ../optimized_ff.cpp
LOCAL_LDLIBS := -llog -lEGL -lGLESv3 -landroid -ldl
LOCAL_CPPFLAGS := -std=c++17 -O2 -fPIC
include $(BUILD_SHARED_LIBRARY)
