CMAKE_MINIMUM_REQUIRED(VERSION 2.6)
PROJECT(libtpl-egl_package_config)

SET(BUILD_ARCH "$ENV{RPM_ARCH}")

IF(${BUILD_ARCH} MATCHES "aarch64")
SET(LIB "lib64")
SET(LIBDIR "/usr/lib64")
ELSEIF(${BUILD_ARCH} MATCHES "x86_64")
SET(LIB "lib64")
SET(LIBDIR "/usr/lib64")
ELSE(${BUILD_ARCH} MATCHES "aarch64")
SET(LIB "lib")
SET(LIBDIR "/usr/lib")
ENDIF(${BUILD_ARCH} MATCHES "aarch64")

#################################################################
# Build Package Config File
# ------------------------------
CONFIGURE_FILE(tpl-egl.pc.in tpl-egl.pc @ONLY)
CONFIGURE_FILE(wayland-egl.pc.in wayland-egl.pc @ONLY)
