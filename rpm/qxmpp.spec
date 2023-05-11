Name:		QXmpp
Summary:    QXmpp Library
Version:    1.5.0
Release:    1
Group:      Qt/Qt
Source:    	master.zip
URL:        https://github.com/ron282/qxmpp.git
License:    LICENSE
BuildRequires: cmake qca-devel omemo-c-devel
Requires: 	qca omemo-c

Patch0:     sfos.diff

%description

%package devel
Summary:        Development package of %{name}
Requires:       %{name} = %{version}
Provides:		%{name}-devel

%description devel
Contains files needed to development with %{name}.

%prep
#%autosetup -p1 -n %{name}-%{version}/%{name}

# sfos.diff
# patch -p1 < %{patch0}

%build
mkdir -p build
pushd build

%cmake .. \
-D BUILD_TESTS=FALSE \
-D BUILD_EXAMPLES=TRUE \
-D BUILD_OMEMO=TRUE \
-D WITH_OMEMO_V03=TRUE

%make_build 
popd

%install
pushd build
%make_install 
cp -Ra %{_sourcedir}/../build/qxmpp.pc %{buildroot}%{_libdir}/pkgconfig
popd 

%post
/sbin/ldconfig

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
