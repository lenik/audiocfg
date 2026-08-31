# Version is injected by packaging/rpm/Makefile via `zfr version`.
# RPM Version cannot contain '-'; use `zfr version -r` (hyphens → '_').
# srcversion is the unsanitized Meson/git version and names the tarball.
%{!?version:%global version 0.0.0}
%{!?srcversion:%global srcversion %{version}}

Name:           audiocfg
Version:        %{version}
Release:        1%{?dist}
Summary:        PulseAudio device and profile configuration utility

License:        AGPL-3.0-or-later
URL:            https://github.com/lenik/audiocfg
Packager:       Lenik <audiocfg@bodz.net>
Source0:        %{name}-%{srcversion}.tar.xz

BuildRequires:  meson
BuildRequires:  ninja-build
BuildRequires:  pkgconf
BuildRequires:  check
BuildRequires:  libbas-c-dev
BuildRequires:  libpulse-dev
BuildRequires:  asciidoctor

%description
audiocfg lists PulseAudio playback and capture devices, shows card
profiles, sets the active profile on a selected card, and can cycle
through a list of profiles with --toggle.

%prep
%setup -q -n %{name}-%{srcversion}

%build
meson setup build \
    --prefix=%{_prefix} \
    --bindir=%{_bindir} \
    --datadir=%{_datadir} \
    --mandir=%{_mandir} \
    --sysconfdir=%{_sysconfdir} \
    --localstatedir=%{_localstatedir} \
    --buildtype=plain
meson compile -C build

%install
meson install -C build --destdir=%{buildroot}

%files
%{_bindir}/audiocfg
%{_datadir}/bash-completion/completions/audiocfg
%{_mandir}/man1/audiocfg.1*
%{_mandir}/*/man1/audiocfg.1*
%{_datadir}/locale/*/LC_MESSAGES/audiocfg.mo
%{_datadir}/doc/%{name}/
%changelog
* Thu Aug 20 2026 Lenik <audiocfg@bodz.net>
- Align spec with debian/control (Meson, AGPL-3.0-or-later).
- Version comes from `zfr version`, the same method meson.build uses.
