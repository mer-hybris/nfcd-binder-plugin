Name: nfcd-binder-plugin
Version: 1.1.1
Release: 0
Summary: Binder-based NFC plugin
Group: Development/Libraries
License: BSD
URL: https://github.com/mer-hybris/nfcd-binder-plugin
Source: %{name}-%{version}.tar.bz2

%define libgbinder_version 1.0.30
%define nfcd_version 1.0.20

BuildRequires: pkgconfig(libncicore)
BuildRequires: pkgconfig(libnciplugin)
BuildRequires: pkgconfig(libgbinder) >= %{libgbinder_version}
BuildRequires: pkgconfig(nfcd-plugin) >= %{nfcd_version}
Requires: libgbinder >= %{libgbinder_version}
Requires: nfcd >= %{nfcd_version}

%description
Binder-based NFC plugin for Android 8+.

%package devel
Summary: Development files for %{name}
Requires: pkgconfig(nfcd-plugin) >= %{nfcd_version}

%description devel
This package contains development files for %{name}.

%prep
%setup -q

%build
make %{_smp_mflags} KEEP_SYMBOLS=1 release pkgconfig

%install
rm -rf %{buildroot}
make install install-dev DESTDIR=%{buildroot}
strip %{buildroot}/%{_libdir}/*.so

%post
systemctl reload-or-try-restart nfcd.service ||:

%postun
systemctl reload-or-try-restart nfcd.service ||:

%files
%defattr(-,root,root,-)
%dir %{_libdir}/nfcd/plugins
%{_libdir}/nfcd/plugins/*.so

%files devel
%defattr(-,root,root,-)
%{_libdir}/pkgconfig/*.pc
%{_libdir}/*.so
%{_includedir}/nfcbinder/*.h
