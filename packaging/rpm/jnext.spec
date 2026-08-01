Name:           jnext
Version:        0.99.128
Release:        1%{?dist}
Summary:        Real-time ZX Spectrum Next emulator with an integrated debugger

License:        GPLv3
URL:            https://github.com/jorgegv/jnext
Source0:        https://github.com/jorgegv/jnext/archive/refs/tags/v%{version}.tar.gz

# Targets Fedora current and previous (per project support policy).
BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  git
BuildRequires:  SDL2-devel
BuildRequires:  zlib-devel
BuildRequires:  libcurl-devel
BuildRequires:  openssl-devel
BuildRequires:  libpng-devel
BuildRequires:  qt6-qtbase-devel
BuildRequires:  desktop-file-utils
BuildRequires:  libappstream-glib

Requires:       hicolor-icon-theme
# ffmpeg (any variant providing the CLI) is invoked as a subprocess by the
# --record feature; not linked, so it is a soft Recommends, not a Requires.
Recommends:     ffmpeg

%description
JNEXT is a real-time, cross-platform ZX Spectrum Next emulator written in
C++17, based on the official VHDL sources for the ZX Spectrum Next FPGA
core. It targets developers writing Next software: a debugger showing CPU,
memory, video layers, sprites, audio and NextREG state live, with
breakpoints, watches and step-backward execution, plus a headless mode for
running programs under CI.

%prep
%autosetup -n %{name}-%{version}

%build
%cmake -DENABLE_QT_UI=ON -DENABLE_TESTS=OFF -DCMAKE_BUILD_TYPE=Release
%cmake_build

%install
%cmake_install
desktop-file-validate %{buildroot}%{_datadir}/applications/io.github.zxjogv.jnext.desktop
appstream-util validate-relax --nonet %{buildroot}%{_datadir}/metainfo/io.github.zxjogv.jnext.metainfo.xml

%files
%license %{_docdir}/%{name}/LICENSE
%doc %{_docdir}/%{name}/README.md
%doc %{_docdir}/%{name}/ChangeLog
%doc %{_docdir}/%{name}/USAGE.md
%doc %{_docdir}/%{name}/user-guide
%{_mandir}/man1/jnext.1*
%{_bindir}/jnext
%{_datadir}/applications/io.github.zxjogv.jnext.desktop
%{_datadir}/metainfo/io.github.zxjogv.jnext.metainfo.xml
%{_datadir}/icons/hicolor/scalable/apps/io.github.zxjogv.jnext.svg
%{_datadir}/icons/hicolor/512x512/apps/io.github.zxjogv.jnext.png

%changelog
* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.128-1
- New release 0.99.128.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.127-1
- New release 0.99.127.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.126-1
- New release 0.99.126.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.125-1
- New release 0.99.125.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.124-1
- New release 0.99.124.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.123-1
- New release 0.99.123.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.122-1
- New release 0.99.122.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.121-1
- New release 0.99.121.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.120-1
- New release 0.99.120.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.119-1
- New release 0.99.119.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.118-1
- New release 0.99.118.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.117-1
- New release 0.99.117.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.116-1
- New release 0.99.116.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.115-1
- New release 0.99.115.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.114-1
- New release 0.99.114.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.113-1
- New release 0.99.113.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.112-1
- New release 0.99.112.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.111-1
- New release 0.99.111.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.110-1
- New release 0.99.110.

* Sat Aug 01 2026 ZXjogv <zx@jogv.es> - 0.99.109-1
- New release 0.99.109.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.108-1
- New release 0.99.108.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.107-1
- New release 0.99.107.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.106-1
- New release 0.99.106.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.105-1
- New release 0.99.105.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.104-1
- New release 0.99.104.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.103-1
- New release 0.99.103.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.102-1
- New release 0.99.102.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.101-1
- New release 0.99.101.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.100-1
- New release 0.99.100.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.99-1
- New release 0.99.99.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.98-1
- New release 0.99.98.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.97-1
- New release 0.99.97.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.96-1
- New release 0.99.96.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.95-1
- New release 0.99.95.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.94-1
- New release 0.99.94.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.93-1
- New release 0.99.93.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.92-1
- New release 0.99.92.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.91-1
- New release 0.99.91.

* Fri Jul 31 2026 ZXjogv <zx@jogv.es> - 0.99.90-1
- New release 0.99.90.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.89-1
- New release 0.99.89.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.88-1
- New release 0.99.88.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.87-1
- New release 0.99.87.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.86-1
- New release 0.99.86.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.85-1
- New release 0.99.85.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.84-1
- New release 0.99.84.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.83-1
- New release 0.99.83.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.82-1
- New release 0.99.82.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.81-1
- New release 0.99.81.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.80-1
- New release 0.99.80.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.79-1
- New release 0.99.79.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.78-1
- New release 0.99.78.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.77-1
- New release 0.99.77.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.76-1
- New release 0.99.76.

* Thu Jul 30 2026 ZXjogv <zx@jogv.es> - 0.99.75-1
- New release 0.99.75.

* Wed Jul 29 2026 ZXjogv <zx@jogv.es> - 0.99.74-1
- New release 0.99.74.

* Wed Jul 29 2026 ZXjogv <zx@jogv.es> - 0.99.73-1
- New release 0.99.73.

* Wed Jul 29 2026 ZXjogv <zx@jogv.es> - 0.99.72-1
- New release 0.99.72.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.71-1
- New release 0.99.71.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.70-1
- New release 0.99.70.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.69-1
- New release 0.99.69.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.68-1
- New release 0.99.68.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.67-1
- New release 0.99.67.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.66-1
- New release 0.99.66.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.65-1
- New release 0.99.65.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.64-1
- New release 0.99.64.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.63-1
- New release 0.99.63.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.62-1
- New release 0.99.62.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.61-1
- New release 0.99.61.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.60-1
- New release 0.99.60.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.59-1
- New release 0.99.59.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.58-1
- New release 0.99.58.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.57-1
- New release 0.99.57.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.56-1
- New release 0.99.56.

* Tue Jul 28 2026 ZXjogv <zx@jogv.es> - 0.99.55-1
- New release 0.99.55.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.54-1
- New release 0.99.54.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.53-1
- New release 0.99.53.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.52-1
- New release 0.99.52.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.51-1
- New release 0.99.51.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.50-1
- New release 0.99.50.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.49-1
- New release 0.99.49.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.48-1
- New release 0.99.48.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.47-1
- New release 0.99.47.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.46-1
- New release 0.99.46.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.45-1
- New release 0.99.45.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.44-1
- New release 0.99.44.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.43-1
- New release 0.99.43.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.42-1
- New release 0.99.42.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.41-1
- New release 0.99.41.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.40-1
- New release 0.99.40.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.39-1
- New release 0.99.39.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.38-1
- New release 0.99.38.

* Mon Jul 27 2026 ZXjogv <zx@jogv.es> - 0.99.37-1
- New release 0.99.37.

* Sun Jul 26 2026 ZXjogv <zx@jogv.es> - 0.99.36-1
- New release 0.99.36.

* Sun Jul 26 2026 ZXjogv <zx@jogv.es> - 0.99.35-1
- New release 0.99.35.

* Sun Jul 26 2026 ZXjogv <zx@jogv.es> - 0.99.34-1
- New release 0.99.34.

* Sun Jul 26 2026 ZXjogv <zx@jogv.es> - 0.99.33-1
- New release 0.99.33.

* Sun Jul 26 2026 ZXjogv <zx@jogv.es> - 0.99.32-1
- New release 0.99.32.

* Sun Jul 26 2026 ZXjogv <zx@jogv.es> - 0.99.31-1
- New release 0.99.31.

* Sun Jul 26 2026 ZXjogv <zx@jogv.es> - 0.99.30-1
- New release 0.99.30.

* Sun Jul 26 2026 ZXjogv <zx@jogv.es> - 0.99.29-1
- New release 0.99.29.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.28-1
- New release 0.99.28.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.27-1
- New release 0.99.27.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.26-1
- New release 0.99.26.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.25-1
- New release 0.99.25.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.24-1
- New release 0.99.24.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.23-1
- New release 0.99.23.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.22-1
- New release 0.99.22.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.21-1
- New release 0.99.21.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.20-1
- New release 0.99.20.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.19-1
- New release 0.99.19.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.18-1
- New release 0.99.18.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.17-1
- New release 0.99.17.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.16-1
- New release 0.99.16.

* Sat Jul 25 2026 ZXjogv <zx@jogv.es> - 0.99.15-1
- New release 0.99.15.

* Fri Jul 24 2026 ZXjogv <zx@jogv.es> - 0.99.14-1
- New release 0.99.14.

* Fri Jul 24 2026 ZXjogv <zx@jogv.es> - 0.99.13-1
- New release 0.99.13.

* Fri Jul 24 2026 ZXjogv <zx@jogv.es> - 0.99.12-1
- New release 0.99.12.

* Fri Jul 24 2026 ZXjogv <zx@jogv.es> - 0.99.11-1
- New release 0.99.11.

* Fri Jul 24 2026 ZXjogv <zx@jogv.es> - 0.99.10-1
- New release 0.99.10.

* Fri Jul 24 2026 ZXjogv <zx@jogv.es> - 0.99.9-1
- New release 0.99.9.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.8-1
- New release 0.99.8.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.7-1
- New release 0.99.7.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.6-1
- New release 0.99.6.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.5-1
- New release 0.99.5.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.4-1
- New release 0.99.4.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.3-1
- New release 0.99.3.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.2-1
- New release 0.99.2.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.1-1
- New release 0.99.1.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.99.0-1
- New release 0.99.0.

* Thu Jul 23 2026 ZXjogv <zx@jogv.es> - 0.98.101-1
- New release 0.98.101.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.100-1
- New release 0.98.100.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.99-1
- New release 0.98.99.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.98-1
- New release 0.98.98.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.97-1
- New release 0.98.97.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.96-1
- New release 0.98.96.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.95-1
- New release 0.98.95.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.94-1
- New release 0.98.94.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.93-1
- New release 0.98.93.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.92-1
- New release 0.98.92.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.91-1
- New release 0.98.91.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.90-1
- New release 0.98.90.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.89-1
- New release 0.98.89.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.88-1
- New release 0.98.88.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.87-1
- New release 0.98.87.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.86-1
- New release 0.98.86.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.85-1
- New release 0.98.85.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.84-1
- New release 0.98.84.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.83-1
- New release 0.98.83.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.82-1
- New release 0.98.82.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.81-1
- New release 0.98.81.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.80-1
- New release 0.98.80.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.79-1
- New release 0.98.79.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.78-1
- New release 0.98.78.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.77-1
- New release 0.98.77.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.76-1
- New release 0.98.76.

* Wed Jul 22 2026 ZXjogv <zx@jogv.es> - 0.98.75-1
- New release 0.98.75.

* Tue Jul 21 2026 ZXjogv <zx@jogv.es> - 0.98.74-1
- New release 0.98.74.

* Tue Jul 21 2026 ZXjogv <zx@jogv.es> - 0.98.73-1
- New release 0.98.73.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.72-1
- New release 0.98.72.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.71-1
- New release 0.98.71.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.70-1
- New release 0.98.70.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.69-1
- New release 0.98.69.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.68-1
- New release 0.98.68.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.67-1
- New release 0.98.67.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.66-1
- New release 0.98.66.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.65-1
- New release 0.98.65.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.64-1
- New release 0.98.64.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.63-1
- New release 0.98.63.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.62-1
- New release 0.98.62.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.61-1
- New release 0.98.61.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.60-1
- New release 0.98.60.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.59-1
- New release 0.98.59.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.58-1
- New release 0.98.58.

* Mon Jul 20 2026 ZXjogv <zx@jogv.es> - 0.98.57-1
- New release 0.98.57.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.56-1
- New release 0.98.56.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.55-1
- New release 0.98.55.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.54-1
- New release 0.98.54.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.53-1
- New release 0.98.53.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.52-1
- New release 0.98.52.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.51-1
- New release 0.98.51.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.50-1
- New release 0.98.50.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.49-1
- New release 0.98.49.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.48-1
- New release 0.98.48.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.47-1
- New release 0.98.47.

* Sun Jul 19 2026 ZXjogv <zx@jogv.es> - 0.98.46-1
- New release 0.98.46.

* Sat Jul 18 2026 ZXjogv <zx@jogv.es> - 0.98.45-1
- New release 0.98.45.

* Sat Jul 18 2026 ZXjogv <zx@jogv.es> - 0.98.44-1
- New release 0.98.44.

* Sat Jul 18 2026 ZXjogv <zx@jogv.es> - 0.98.43-1
- New release 0.98.43.

* Sat Jul 18 2026 ZXjogv <zx@jogv.es> - 0.98.42-1
- New release 0.98.42.

* Sat Jul 18 2026 ZXjogv <zx@jogv.es> - 0.98.41-1
- New release 0.98.41.

* Sat Jul 18 2026 ZXjogv <zx@jogv.es> - 0.98.40-1
- New release 0.98.40.

* Sat Jul 18 2026 ZXjogv <zx@jogv.es> - 0.98.39-1
- New release 0.98.39.

* Sat Jul 18 2026 ZXjogv <zx@jogv.es> - 0.98.38-1
- New release 0.98.38.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.37-1
- New release 0.98.37.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.36-1
- New release 0.98.36.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.35-1
- New release 0.98.35.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.34-1
- New release 0.98.34.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.33-1
- New release 0.98.33.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.32-1
- New release 0.98.32.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.31-1
- New release 0.98.31.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.30-1
- New release 0.98.30.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.29-1
- New release 0.98.29.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.28-1
- New release 0.98.28.

* Fri Jul 17 2026 ZXjogv <zx@jogv.es> - 0.98.27-1
- New release 0.98.27.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.26-1
- New release 0.98.26.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.25-1
- New release 0.98.25.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.24-1
- New release 0.98.24.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.23-1
- New release 0.98.23.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.22-1
- New release 0.98.22.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.21-1
- New release 0.98.21.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.20-1
- New release 0.98.20.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.19-1
- New release 0.98.19.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.18-1
- New release 0.98.18.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.17-1
- New release 0.98.17.

* Thu Jul 16 2026 ZXjogv <zx@jogv.es> - 0.98.16-1
- New release 0.98.16.

* Wed Jul 15 2026 ZXjogv <zx@jogv.es> - 0.98.5-1
- Initial packaging (Task 67)
