# The version is NOT written in this file: version.yaml is the single source of
# truth, so a bump never edits the spec. Pass it in at build time:
#   rpmbuild -bb --define "jnext_version $(awk '/^version:/{print $2}' version.yaml)" \
#       packaging/rpm/jnext.spec
# Without it the build stops here rather than packaging a wrong or empty version.
%{!?jnext_version:%{error:jnext_version is not defined. Pass --define "jnext_version X.Y.Z" with the version from version.yaml (see packaging/README.md).}}

Name:           jnext
Version:        %{jnext_version}
Release:        1%{?dist}
Summary:        Real-time ZX Spectrum Next emulator with an integrated debugger

License:        GPLv3
URL:            https://github.com/jorgegv/jnext
Source0:        https://github.com/jorgegv/jnext/archive/refs/tags/v%{version}.tar.gz

# Targets Fedora current and previous (per project support policy).
BuildRequires:  cmake >= 3.16
BuildRequires:  gcc-c++
BuildRequires:  git
BuildRequires:  SDL3-devel
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

# The top changelog entry is generated at build time for the version being
# built, dated today (or SOURCE_DATE_EPOCH, when set, for a reproducible build),
# so it always matches Version: and no bump has to prepend one. LC_ALL=C because
# rpm parses this date and only accepts English day and month names. Release
# notes are the ChangeLog file this package installs as documentation.
%changelog
* %(LC_ALL=C date -u -d "@${SOURCE_DATE_EPOCH:-$(date +%%s)}" '+%%a %%b %%d %%Y') ZXjogv <zx@jogv.es> - %{version}-1
- Release %{version}.

* Wed Jul 15 2026 ZXjogv <zx@jogv.es> - 0.98.5-1
- Initial packaging (Task 67)
