#$(call is-feature-enabled,featurename)
#returns non-empty string if enabled, empty if not
define is-feature-enabled
$(findstring -$1-,-$(TPL_OPTIONS)-)
endef

SRC_DIR = ./src
SO_NAME = libtpl-egl.so.$(TPL_VER_MAJOR).$(TPL_VER_MINOR)
BIN_NAME = $(SO_NAME).$(TPL_RELEASE)
INST_DIR = $(libdir)

CC ?= gcc

CFLAGS += -Wall -fPIC -I$(SRC_DIR)
LDFLAGS +=

CFLAGS += `pkg-config --cflags libdrm libtbm dlog`
LDFLAGS += `pkg-config --libs libdrm libtbm dlog`

ifneq ($(call is-feature-enabled,winsys_dri2),)
	CFLAGS += -DTPL_WINSYS_DRI2
	LDFLAGS += `pkg-config --libs libdri2 xext xfixes x11 x11-xcb xcb xcb-dri3 xcb-sync xcb-present xshmfence`
endif
ifneq ($(call is-feature-enabled,winsys_dri3),)
	CFLAGS += -DTPL_WINSYS_DRI3
	LDFLAGS += `pkg-config --libs libdri2 xext xfixes x11 x11-xcb xcb xcb-dri3 xcb-sync xcb-present xshmfence`
endif

ifneq ($(call is-feature-enabled,winsys_wl),)
	CFLAGS += -DTPL_WINSYS_WL=1
	CFLAGS += `pkg-config --cflags gbm libtdm-client`
	LDFLAGS += `pkg-config --libs gbm wayland-tbm-client wayland-tbm-server libtdm-client`
endif

ifneq ($(call is-feature-enabled,winsys_tbm),)
	CFLAGS += -DTPL_WINSYS_TBM=1
endif

ifneq ($(call is-feature-enabled,ttrace),)
	CFLAGS += -DTTRACE_ENABLE=1
	CFLAGS += `pkg-config --cflags ttrace`
	LDFLAGS += `pkg-config --libs ttrace`
endif
ifneq ($(call is-feature-enabled,dlog),)
	CFLAGS += -DDLOG_DEFAULT_ENABLE
endif
ifneq ($(call is-feature-enabled,default_log),)
	CFLAGS += -DLOG_DEFAULT_ENABLE
endif
ifneq ($(call is-feature-enabled,default_dump),)
	CFLAGS += -DDEFAULT_DUMP_ENABLE
endif
ifneq ($(call is-feature-enabled,object_hash_check),)
	CFLAGS += -DOBJECT_HASH_CHECK
endif

ifneq ($(call is-feature-enabled,arm_atomic_operation),)
	CFLAGS += -DARM_ATOMIC_OPERATION
endif

TPL_HEADERS += $(SRC_DIR)/tpl.h
TPL_HEADERS += $(SRC_DIR)/tpl_internal.h
TPL_HEADERS += $(SRC_DIR)/tpl_utils.h

TPL_SRCS += $(SRC_DIR)/tpl.c
TPL_SRCS += $(SRC_DIR)/tpl_display.c
TPL_SRCS += $(SRC_DIR)/tpl_object.c
TPL_SRCS += $(SRC_DIR)/tpl_surface.c
TPL_SRCS += $(SRC_DIR)/tpl_utils_hlist.c
TPL_SRCS += $(SRC_DIR)/tpl_utils_map.c

ifneq ($(call is-feature-enabled,winsys_wl),)
TPL_HEADERS += $(SRC_DIR)/tpl_worker_thread.h
TPL_SRCS += $(SRC_DIR)/tpl_wayland_egl.c
TPL_SRCS += $(SRC_DIR)/tpl_wayland_vk_wsi.c
TPL_SRCS += $(SRC_DIR)/tpl_gbm.c
TPL_SRCS += $(SRC_DIR)/protocol/tizen-surface-protocol.c
TPL_SRCS += $(SRC_DIR)/tpl_worker_thread.c
TPL_SRCS += $(SRC_DIR)/wayland-vulkan/wayland-vulkan-protocol.c
endif

ifneq ($(call is-feature-enabled,winsys_dri2),)
TPL_HEADERS += $(SRC_DIR)/tpl_x11_internal.h

TPL_SRCS += $(SRC_DIR)/tpl_x11_common.c
TPL_SRCS += $(SRC_DIR)/tpl_x11_dri2.c
endif

ifneq ($(call is-feature-enabled,winsys_dri3),)
TPL_HEADERS += $(SRC_DIR)/tpl_x11_internal.h

TPL_SRCS += $(SRC_DIR)/tpl_x11_common.c
TPL_SRCS += $(SRC_DIR)/tpl_x11_dri3.c
endif

ifneq ($(call is-feature-enabled,winsys_tbm),)
TPL_SRCS += $(SRC_DIR)/tpl_tbm.c
endif

OBJS = $(TPL_SRCS:%.c=%.o)

################################################################################
all: $(BIN_NAME)

$(BIN_NAME) : $(OBJS) $(TPL_HEADERS)
	$(CC) -o $@ $(OBJS) -shared -Wl,-soname,$(SO_NAME) $(CFLAGS) $(LDFLAGS)

%.o: %.c
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	find . -name "*.o" -exec rm -vf {} \;
	find . -name "*~" -exec rm -vf {} \;
	rm -vf $(BIN_NAME)

install: all
	cp -va $(BIN_NAME) $(INST_DIR)/

uninstall:
	rm -f $(INST_DIR)/$(BIN_NAME)
