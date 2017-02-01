#######################################################
##### Makefile to build tpl-test using libgtest.a #####
#######################################################

BIN_NAME := tpl-test

CXX ?= g++


########## Helper Functions ##########
#$(call is-feature-enabled,featurename)
#returns non-empty string if enabled, empty if not
define is-feature-enabled
$(findstring -$1-,-$(TPL_OPTIONS)-)
endef


########## Build Environments ##########
TC_DIR := $(shell pwd)

TC_CXXFLAGS =
TC_LDFLAGS =

# src include
TC_CXXFLAGS += -I./
TC_CXXFLAGS += -I../src/

# gtest
GTEST_DIR := $(TC_DIR)/libs/gtest
GTEST_INCLUDE := -I$(GTEST_DIR)/googletest -I$(GTEST_DIR)/googletest/include \
				 -I$(GTEST_DIR)/googlemock -I$(GTEST_DIR)/googlemock/include
GTEST_FLAGS := -g -Wall -pthread
GTEST_LIB_PATH := $(GTEST_DIR)/build/gtest/libgtest.a	# googletest output path
GMOCK_LIB_PATH := $(GTEST_DIR)/build/libgmock.a			# googlemock output path

# libtpl-egl, libwayland-egl
TPL_LIB_PATH = ../libtpl-egl.so.$(TPL_VER_MAJOR).$(TPL_VER_MINOR).$(TPL_RELEASE)
WAYLAND_EGL_OBJ_PATH = ../src/wayland-egl/wayland-egl.o


########## Backend Setup ##########
ifneq ($(call is-feature-enabled,winsys_wl),)
	TC_CXXFLAGS += `pkg-config --cflags gbm libtdm-client`
	TC_LDFLAGS += `pkg-config --libs gbm wayland-tbm-client wayland-tbm-server libtdm-client`
endif


########## tpl-test srcs ##########
SRCS = $(wildcard src/*.cpp)
SRCS += $(wildcard mocked/*.cpp)
HEADERS = $(wildcard src/*.h)
HEADERS += $(wildcard mocked/*.h)


########## tpl-test test cases srcs ##########
TESTS	= $(wildcard test/*.cpp)

OBJS	= $(SRCS:%.cpp=%.o) \
		  $(TESTS:%.cpp=%.o) \
		  $(NULL)


########## Build Commands ##########
all : gtest $(BIN_NAME)

gtest :
	mkdir $(GTEST_DIR)/build && \
	cd $(GTEST_DIR)/build && \
	cmake ../googlemock && \
	make

clean :
	rm -f $(BIN_NAME)

%.o: %.cpp
	$(CXX) -c -o $@ $< $(GTEST_INCLUDE) $(GTEST_FLAGS) $(TC_CXXFLAGS) $(TC_LDFLAGS)

$(BIN_NAME) : $(OBJS) $(GTEST_LIB_PATH) $(GMOCK_LIB_PATH) $(TPL_LIB_PATH) $(WAYLAND_EGL_OBJ_PATH)
	cd ${TC_DIR} && \
	$(CXX) -lpthread $^ -o $@ $(GTEST_FLAGS) $(TC_CXXFLAGS) $(TC_LDFLAGS)

