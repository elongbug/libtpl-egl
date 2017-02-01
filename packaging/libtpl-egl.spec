#TPL VERSION MACROS
%define TPL_VER_MAJOR	1
%define TPL_VER_MINOR	0
%define TPL_RELEASE	0
%define TPL_VERSION	%{TPL_VER_MAJOR}.%{TPL_VER_MINOR}
%define TPL_VER_FULL	%{TPL_VERSION}.%{TPL_RELEASE}

#TPL WINDOW SYSTEM DEFINITION
%define TPL_WINSYS	WL

#TPL FEATURE OPTION
%define ENABLE_TTRACE	0
%define ENABLE_DLOG	1
%define ENABLE_DEFAULT_LOG	0
%define ENABLE_DEFAULT_DUMP	0
%define ENABLE_OBJECT_HASH_CHECK	1

#TPL INSTALL OPTION
%define ENABLE_TPL_TEST	0

#WAYLAND-EGL VERSION MACROS
%define WL_EGL_VERSION	1.0.0

#TPL WINDOW SYSTEM CHECK
%if "%{TPL_WINSYS}" != "DRI2" && "%{TPL_WINSYS}" != "DRI3" && "%{TPL_WINSYS}" != "WL"
BuildRequires:		ERROR(No_window_system_designated)
%endif

#Exclusive Emulator Arch
%if "%{_with_emulator}" == "1"
ExclusiveArch: %{ix86} x86_64
%else
ExclusiveArch: %{arm} aarch64
%endif

Name:		libtpl-egl
Version:	%{TPL_VERSION}
Release:	%{TPL_RELEASE}
%if "%{TPL_WINSYS}" == "DRI2"
Summary:	Tizen Porting Layer for EGL (DRI2 backend)
%endif
%if "%{TPL_WINSYS}" == "DRI3"
Summary:	Tizen Porting Layer for EGL (DRI3 backend)
%endif
%if "%{TPL_WINSYS}" == "WL"
Summary:	Tizen Porting Layer for EGL (Wayland backend)
%endif
Group: Graphics & UI Framework/GL
# The entire source code is MIT except tc/libs/gtest/ which is BSD-3-Clause
License:	MIT and BSD-3-Clause
Source:		%{name}-%{version}.tar.gz

BuildRequires:	cmake
BuildRequires:	pkg-config
BuildRequires:	pkgconfig(libdrm)
BuildRequires:	pkgconfig(libtbm)
BuildRequires:	pkgconfig(dlog)

%if "%{TPL_WINSYS}" == "DRI2" || "%{TPL_WINSYS}" == "DRI3"
BuildRequires:	pkgconfig(libdri2)
BuildRequires:	pkgconfig(xext)
BuildRequires:	pkgconfig(xfixes)
BuildRequires:	pkgconfig(x11)
BuildRequires:	pkgconfig(x11-xcb)
BuildRequires:	pkgconfig(xcb)
BuildRequires:	pkgconfig(xcb-dri3)
BuildRequires:	pkgconfig(xcb-sync)
BuildRequires:	pkgconfig(xcb-present)
BuildRequires:	pkgconfig(xshmfence)
%endif

%if "%{TPL_WINSYS}" == "WL"
BuildRequires:  libtool
BuildRequires:  wayland-devel
BuildRequires:	pkgconfig(gbm)
BuildRequires:	pkgconfig(libtdm-client)
BuildRequires:	pkgconfig(wayland-tbm-client)
BuildRequires:  pkgconfig(wayland-tbm-server)
%endif

%if "%{ENABLE_TTRACE}" == "1"
BuildRequires: pkgconfig(ttrace)
%endif

%global TZ_SYS_RO_SHARE  %{?TZ_SYS_RO_SHARE:%TZ_SYS_RO_SHARE}%{!?TZ_SYS_RO_SHARE:/usr/share}

%description
Tizen Porting Layer (a.k.a TPL) is a linkage between the underlying window
system and the EGL porting layer.

The following window systems are supported:
- X11 DRI2/DRI3
- Wayland

%package devel
Summary:	Development files for TPL
Group:		System/Libraries
Requires:	%{name} = %{version}-%{release}
%if "%{TPL_WINSYS}" == "WL"
Requires:   libwayland-egl-devel
%endif

%description devel
This package contains the development libraries and header files needed by
the GPU Vendor DDK's EGL.

%if "%{TPL_WINSYS}" == "WL"
%package -n libwayland-egl
Summary:    Wayland EGL backend

%description -n libwayland-egl
Wayland EGL backend

%package -n libwayland-egl-devel
Summary:    Development header files for use with Wayland protocol
Requires: libwayland-egl

%description -n libwayland-egl-devel
Development header files for use with Wayland protocol
%endif

%prep
%setup -q

%build
#libtpl-egl build
TPL_OPTIONS=${TPL_OPTIONS}-winsys_tbm

%if "%{TPL_WINSYS}" == "DRI2"
TPL_OPTIONS=${TPL_OPTIONS}-winsys_dri2
%endif
%if "%{TPL_WINSYS}" == "DRI3"
TPL_OPTIONS=${TPL_OPTIONS}-winsys_dri3
%endif
%if "%{TPL_WINSYS}" == "WL"
TPL_OPTIONS=${TPL_OPTIONS}-winsys_wl
%endif

%if "%{ENABLE_TTRACE}" == "1"
TPL_OPTIONS=${TPL_OPTIONS}-ttrace
%endif

%if "%{ENABLE_DLOG}" == "1"
TPL_OPTIONS=${TPL_OPTIONS}-dlog
%endif

%if "%{ENABLE_DEFAULT_LOG}" == "1"
TPL_OPTIONS=${TPL_OPTIONS}-default_log
%endif

%if "%{ENABLE_DEFAULT_DUMP}" == "1"
TPL_OPTIONS=${TPL_OPTIONS}-default_dump
%endif

%if "%{ENABLE_OBJECT_HASH_CHECK}" == "1"
TPL_OPTIONS=${TPL_OPTIONS}-object_hash_check
%endif

%ifarch %arm aarch64
TPL_OPTIONS=${TPL_OPTIONS}-arm_atomic_operation
%endif

# do not change the following line
export TPL_OPTIONS=${TPL_OPTIONS}-

export TPL_VER_MAJOR=%{TPL_VER_MAJOR}
export TPL_VER_MINOR=%{TPL_VER_MINOR}
export TPL_RELEASE=%{TPL_RELEASE}

#wayland-vulkan build
%if "%{TPL_WINSYS}" == "WL"
cd src/wayland-vulkan
make
cd -
%endif

make all

#libwayland-egl build
%if "%{TPL_WINSYS}" == "WL"
cd src/wayland-egl
export WLD_EGL_SO_VER=%{WL_EGL_VERSION}
make
cd ../../
%endif

#tpl-test build
%if "%{ENABLE_TPL_TEST}" == "1"
cd tc/
make
cd ../
%endif

#pkgconfig configure
cd pkgconfig
cmake .
cd ..

%install
rm -fr %{buildroot}
mkdir -p %{buildroot}
mkdir -p %{buildroot}%{_libdir}
mkdir -p %{buildroot}%{_includedir}
mkdir -p %{buildroot}%{_libdir}/pkgconfig

export TPL_VER_MAJOR=%{TPL_VER_MAJOR}
export TPL_VER_MINOR=%{TPL_VER_MINOR}
export TPL_RELEASE=%{TPL_RELEASE}

make install libdir=%{buildroot}%{_libdir}
ln -sf libtpl-egl.so.%{TPL_VER_FULL}	%{buildroot}%{_libdir}/libtpl-egl.so.%{TPL_VERSION}
ln -sf libtpl-egl.so.%{TPL_VERSION}	%{buildroot}%{_libdir}/libtpl-egl.so.%{TPL_VER_MAJOR}
ln -sf libtpl-egl.so.%{TPL_VER_MAJOR}	%{buildroot}%{_libdir}/libtpl-egl.so

cp -a src/tpl.h				%{buildroot}%{_includedir}/
cp -a pkgconfig/tpl-egl.pc		%{buildroot}%{_libdir}/pkgconfig/

mkdir -p %{buildroot}/%{TZ_SYS_RO_SHARE}/license
cp -a %{_builddir}/%{buildsubdir}/COPYING %{buildroot}/%{TZ_SYS_RO_SHARE}/license/%{name}

%if "%{TPL_WINSYS}" == "WL"
cd src/wayland-egl
cp libwayland-egl.so.%{WL_EGL_VERSION} %{buildroot}%{_libdir}/libwayland-egl.so
cp libwayland-egl.so.%{WL_EGL_VERSION} %{buildroot}%{_libdir}/libwayland-egl.so.1
cp libwayland-egl.so.%{WL_EGL_VERSION} %{buildroot}%{_libdir}/libwayland-egl.so.1.0
cp -a %{_builddir}/%{buildsubdir}/COPYING %{buildroot}/%{TZ_SYS_RO_SHARE}/license/libwayland-egl
export WLD_EGL_SO_VER=%{WL_EGL_VERSION}
%makeinstall
cd -

cd src/wayland-vulkan
%makeinstall
cd -
%endif

#tpl-test install
%if "%{ENABLE_TPL_TEST}" == "1"
mkdir -p %{buildroot}/opt/usr/tpl-test
cp -arp ./tc/tpl-test %{buildroot}/opt/usr/tpl-test

# License of Google Test which is BSD-3-Clause
mkdir -p %{buildroot}/%{TZ_SYS_RO_SHARE}/license
cp -a %{_builddir}/%{buildsubdir}/tc/libs/gtest/googletest/LICENSE %{buildroot}/%{TZ_SYS_RO_SHARE}/license/googletest
%endif

%post -p /sbin/ldconfig
%postun -p /sbin/ldconfig

%if "%{TPL_WINSYS}" == "WL"
%post   -n libwayland-egl -p /sbin/ldconfig
%postun -n libwayland-egl -p /sbin/ldconfig
%endif

%files
%manifest packaging/libtpl-egl.manifest
%{TZ_SYS_RO_SHARE}/license/%{name}
%defattr(-,root,root,-)
%{_libdir}/libtpl-egl.so
%{_libdir}/libtpl-egl.so.%{TPL_VER_MAJOR}
%{_libdir}/libtpl-egl.so.%{TPL_VERSION}
%{_libdir}/libtpl-egl.so.%{TPL_VER_FULL}

#tpl-test files
%if "%{ENABLE_TPL_TEST}" == "1"
%dir /opt/usr/tpl-test/
/opt/usr/tpl-test/tpl-test

# License of Google Test which is BSD-3-Clause
%{TZ_SYS_RO_SHARE}/license/googletest
%endif

%files devel
%defattr(-,root,root,-)
%{_includedir}/tpl.h
%{_libdir}/pkgconfig/tpl-egl.pc

%if "%{TPL_WINSYS}" == "WL"
%files -n libwayland-egl
%manifest packaging/libwayland-egl.manifest
%{TZ_SYS_RO_SHARE}/license/libwayland-egl
%defattr(-,root,root,-)
%{_libdir}/libwayland-egl.so
%{_libdir}/libwayland-egl.so.1
%{_libdir}/libwayland-egl.so.1.0
%{_libdir}/libwayland-egl.so.%{WL_EGL_VERSION}

%files -n libwayland-egl-devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/wayland-egl.pc
%endif
