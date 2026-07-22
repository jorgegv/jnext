Name:           jnext
Version:        0.98.83
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
