# audiocfg

`audiocfg` 是一个用于 PulseAudio 的小型命令行工具：列出播放与采集设备、列出声卡配置文件，并在指定声卡上切换活动配置文件。

## 用法

```bash
audiocfg --list
audiocfg --list-profiles
audiocfg --list-profiles --device 1
audiocfg --list-profiles --device playback:48
audiocfg --device 1 --profile 2
audiocfg --device playback:48 --profile output:hdmi-stereo
audiocfg --device playback:48 --toggle output:hdmi-stereo,off,output:hdmi-surround
```

### 选项

- `-l`, `--list` — 列出设备（1 起始索引，随后为类型、名称、描述、`(card N)`；暂停、不可用或未由当前配置文件暴露的设备会标注 `(disabled)`）
- `-d`, `--device=DEVICE` — 来自 `--list` 的索引、`:CARD`、`playback:CARD`、`capture:CARD` 或名称
- `-L`, `--list-profiles` — 列出声卡与配置文件索引（`*` 表示当前活动配置文件）
- `-p`, `--profile=PROFILE` — 来自 `--list-profiles` 的配置文件索引或配置文件名称
- `-t`, `--toggle=PROFILES` — 在逗号分隔的配置文件列表中循环切换（最后一项后回到第一项）
- `-v`, `--verbose` / `-q`, `--quiet` — 日志级别
- `-h`, `--help` / `--version`

配置文件是 PulseAudio **声卡**的属性。使用 `--device` 指定声卡标识符，或指定该声卡上的 sink/source。

## 构建与测试

### 构建依赖

```bash
sudo apt install meson ninja-build gcc pkg-config check libbas-c-dev libpulse-dev
```

### 配置并构建

```bash
meson setup /build
ninja -C /build
```

### 运行测试

```bash
meson test -C /build
```

## i18n（gettext）

翻译文件位于 `po/` 目录。同步词条：

```bash
ninja -C /build posync
```

## 安装

```bash
meson install -C /build
```

在已配置的安装前缀下调试符号链接：

```bash
ninja -C /build install-symlinks
ninja -C /build uninstall-symlinks
```

## Debian 打包

```bash
dpkg-buildpackage -us -uc
```

## 许可证

Copyright (C) 2026 Lenik <audiocfg@bodz.net>

采用 **AGPL-3.0-or-later** 许可。
