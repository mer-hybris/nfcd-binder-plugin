Name: nfcd-binder-plugin

Version: 1.1.9
Release: 0
Summary: Binder-based NFC plugin
License: BSD
URL: https://github.com/mer-hybris/nfcd-binder-plugin
Source: %{name}-%{version}.tar.bz2

%define libgbinder_version 1.0.30
%define nfcd_version 1.0.20

BuildRequires: pkgconfig
BuildRequires: pkgconfig(libncicore)
BuildRequires: pkgconfig(libnciplugin)
BuildRequires: pkgconfig(libgbinder) >= %{libgbinder_version}
BuildRequires: pkgconfig(nfcd-plugin) >= %{nfcd_version}

# license macro requires rpm >= 4.11
BuildRequires: pkgconfig(rpm)
%define license_support %(pkg-config --exists 'rpm >= 4.11'; echo $?)

Requires: libgbinder >= %{libgbinder_version}
Requires: nfcd >= %{nfcd_version}

%define plugin_dir %{_libdir}/nfcd/plugins

%description
Binder-based NFC plugin for Android 8+.

%prep
%setup -q

%build
make %{_smp_mflags} \
    %{?disable_hexdump: DISABLE_HEXDUMP=1} \
    KEEP_SYMBOLS=1 \
    release

%install
rm -rf %{buildroot}
make DESTDIR=%{buildroot} PLUGIN_DIR=%{plugin_dir} install

%post
systemctl reload-or-try-restart nfcd.service ||:

%postun
systemctl reload-or-try-restart nfcd.service ||:

%files
%defattr(-,root,root,-)
%dir %{plugin_dir}
%{plugin_dir}/*.so
%if %{license_support} == 0
%license LICENSE
%endif
