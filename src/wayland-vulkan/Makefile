.SUFFIXES: .c .o .a

CFLAGS += -Wall -fno-strict-aliasing -Wno-strict-aliasing -Wno-long-long -O3 -fPIC

WAYLAND_VULKAN_PROTOCOLS = wayland-vulkan-protocol.xml

wayland-vulkan-protocol.c : $(WAYLAND_VULKAN_PROTOCOLS)
		wayland-scanner code < $< > $@

wayland-vulkan-server-protocol.h : $(WAYLAND_VULKAN_PROTOCOLS)
		wayland-scanner server-header < $< > $@

wayland-vulkan-client-protocol.h : $(WAYLAND_VULKAN_PROTOCOLS)
		wayland-scanner client-header < $< > $@

.PHONY: all clean client server
.DEFAULT_GOAL = all
all: client server

client: wayland-vulkan-protocol.c wayland-vulkan-client-protocol.h

server: wayland-vulkan-protocol.c wayland-vulkan-server-protocol.h

clean:
		rm -rf wayland-vulkan-protocol.c \
			wayland-vulkan-server-protocol.h \
			wayland-vulkan-client-protocol.h

install: all

