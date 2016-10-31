Name: tpl-test
Version: 0.3.0
Release: 0
Summary: TPL Test Module

Group: Graphics & UI Framework/GL

# The entire source code is MIT except tc/libs/gtest/ which is BSD-3-Clause
License: MIT and BSD-3-Clause
Source0: %{name}-%{version}.tar.gz

BuildRequires: pkgconfig(libtbm)
BuildRequires: pkgconfig(gbm)
BuildRequires: pkgconfig(wayland-client)
BuildRequires: pkgconfig(tpl-egl)
BuildRequires: cmake

%global TZ_SYS_RO_SHARE  %{?TZ_SYS_RO_SHARE:%TZ_SYS_RO_SHARE}%{!?TZ_SYS_RO_SHARE:/usr/share}


%description
Test module for testing libtpl-egl frontend APIs


%prep
%setup -q


%build
###### Setup variables for build ######
export BUILD_DIR="%{_builddir}/%{buildsubdir}"
export GTEST_DIR="${BUILD_DIR}/tc/libs/gtest"

export GTEST_INCLUDE="-I${GTEST_DIR}/googletest -I${GTEST_DIR}/googletest/include \
					  -I${GTEST_DIR}/googlemock -I${GTEST_DIR}/googlemock/include"
export GTEST_FLAGS="-g -Wall -Wextra -pthread"

export GTEST_LIB_PATH="${GTEST_DIR}/build/gtest/libgtest.a"	# googletest output path
export GMOCK_LIB_PATH="${GTEST_DIR}/build/libgmock.a"	# googlemock output path

export BIN_NAME="tpl-test"


##### Build Google Test Framework #####
mkdir ${GTEST_DIR}/build
cd ${GTEST_DIR}/build
cmake ../googlemock
make

##### Build tpl-test using libgtest.a ######
cd ${BUILD_DIR}/tc/
make


%pre
if [ "$1" -eq 1 ]; then
echo "Initial installation"
  # Perform tasks to prepare for the initial installation
elif [ "$1" -eq 2 ]; then
  # Perform whatever maintenance must occur before the upgrade begins
rm -rf /opt/usr/tpl-test
fi


%install
##### Install Binary #####
mkdir -p %{buildroot}/opt/usr/tpl-test
cp -arp ./tc/tpl-test %{buildroot}/opt/usr/tpl-test

##### Licenses #####
mkdir -p %{buildroot}/%{TZ_SYS_RO_SHARE}/license
# MIT
cp -a %{_builddir}/%{buildsubdir}/COPYING %{buildroot}/%{TZ_SYS_RO_SHARE}/license/%{name}
# BSD-3-Clause
cp -a %{_builddir}/%{buildsubdir}/tc/libs/gtest/googletest/LICENSE %{buildroot}/%{TZ_SYS_RO_SHARE}/license/googletest


%post
/opt/usr/tpl-test/tpl-test --gtest_filter=TPL*
if [ "$?" -eq 0 ]; then
echo "Verification of libtpl-egl has been successfully done."
else
echo "Verification of libtpl-egl has been failed."
fi


%clean
rm -rf %{buildroot}


%files
%defattr(-,root,root,-)
##### Binary Files #####
%dir /opt/usr/tpl-test/
/opt/usr/tpl-test/*

##### Licenses ######
# MIT
%{TZ_SYS_RO_SHARE}/license/%{name}
# BSD-3-Clause
%{TZ_SYS_RO_SHARE}/license/googletest
