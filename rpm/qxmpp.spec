# SPDX-FileCopyrightText: 2024 Ron282 <ronan35@gmx.fr>
#
# SPDX-License-Identifier: LGPL-2.1-or-later

Name:       QXmpp
Summary:    QXmpp Library
Version:    1.6.0
Release:    4
Group:      Qt/Qt
Source:     master.zip
URL:        https://github.com/ron282/qxmpp.git
License:    LGPLv2+
BuildRequires: cmake qca-devel omemo-c-devel qt5-qttest-devel
Requires:   qca omemo-c

%description
QXmpp is a cross-platform C++ XMPP client and server library. It is written in C++ and uses Qt framework.

QXmpp strives to be as easy to use as possible, the underlying TCP socket, the core XMPP RFCs (RFC6120 and RFC6121) and XMPP extensions have been nicely encapsulated into classes. QXmpp is ready to build XMPP clients complying with the XMPP Compliance Suites 2022 for IM and Advanced Mobile. It comes with full API documentation, automatic tests and some examples.

QXmpp uses Qt extensively, and as such users need to a have working knowledge of C++ and Qt basics (Signals and Slots and Qt data types).

Qt is the only third party library which is required to build QXmpp, but libraries such as GStreamer enable additional features.

%package devel
Summary:        Development package of %{name}
Requires:       %{name} = %{version}-%{release}
Provides:	%{name}-devel

%description devel
Contains files needed to development with %{name}.

%prep
%autosetup -p1 -n %{name}-%{version}/%{name}

%build
mkdir -p build
pushd build

%cmake .. \
-D BUILD_TESTS=TRUE \
-D BUILD_EXAMPLES=TRUE \
-D BUILD_OMEMO=TRUE \
-D QXMPP_LOGGABLE_TRACE=FALSE \
-D WITH_OMEMO_V03=TRUE \
-D SFOS=TRUE \
-Wdeprecated-declarations

%make_build 
popd

%install
pushd build
%make_install 
cp -Ra %{_sourcedir}/../build/qxmpp.pc %{buildroot}%{_libdir}/pkgconfig
popd 

%postun
/sbin/ldconfig

%files 
%defattr(-, root, root, -)
%{_libdir}/lib%{name}OmemoQt5.so
%{_libdir}/lib%{name}OmemoQt5.so.*
%{_libdir}/lib%{name}Qt5.so
%{_libdir}/lib%{name}Qt5.so.*

%files devel
%defattr(-, root, root, -)
%{_libdir}/pkgconfig/*.pc
%{_includedir}/*/*.h
%{_includedir}/*/*.cpp
%{_includedir}/*/*/*.h
%{_libdir}/cmake/*/*.cmake

