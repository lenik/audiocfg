# audiocfg

`audiocfg` is a small command-line utility for PulseAudio: list playback and
capture devices, list card profiles, and switch the active profile on a card.

## Usage

```bash
audiocfg --list
audiocfg --list-profiles
audiocfg --list-profiles --device 1
audiocfg --list-profiles --device playback:48
audiocfg --device 1 --profile 2
audiocfg --device playback:48 --profile output:hdmi-stereo
audiocfg --device playback:48 --toggle output:hdmi-stereo,off,output:hdmi-surround
```

### Options

- `-l`, `--list` — list devices (1-based index, then type, name, description, `(card N)`; `(disabled)` when suspended, unavailable, or not exposed by the active profile)
- `-d`, `--device=DEVICE` — index from `--list`, `:CARD`, `playback:CARD`, `capture:CARD`, or name
- `-L`, `--list-profiles` — list card and profile indices (`*` = active profile)
- `-p`, `--profile=PROFILE` — profile index from `--list-profiles` or profile name
- `-t`, `--toggle=PROFILES` — cycle through comma-separated profiles (wraps to first after last)
- `-v`, `--verbose` / `-q`, `--quiet` — logging level
- `-h`, `--help` / `--version`

Profiles are properties of PulseAudio **cards**. Use `--device` with a card
identifier, or with a sink/source on that card.

## Build and test

### Build dependencies

```bash
sudo apt install meson ninja-build gcc pkg-config check libbas-c-dev libpulse-dev
```

### Configure and build

```bash
meson setup /build
ninja -C /build
```

### Run tests

```bash
meson test -C /build
```

## i18n (gettext)

Translation catalogs live under `po/`. Sync with:

```bash
ninja -C /build posync
```

## Install

```bash
meson install -C /build
```

Debug symlinks under the configured prefix:

```bash
ninja -C /build install-symlinks
ninja -C /build uninstall-symlinks
```

## License

Copyright (C) 2026 Lenik <audiocfg@bodz.net>

Licensed under **AGPL-3.0-or-later**.
