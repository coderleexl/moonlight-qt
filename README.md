# Desk

## 桌面界面重构版

由 [@coderleexl](https://github.com/coderleexl) 维护的 Moonlight Qt 界面重构版本，面向远程办公并兼顾游戏串流，基于 [上游 Moonlight Qt](https://github.com/moonlight-stream/moonlight-qt)。

[下载 Desk](https://github.com/coderleexl/moonlight-qt/releases) · [本仓库源码](https://github.com/coderleexl/moonlight-qt) · [问题反馈](https://github.com/coderleexl/moonlight-qt/issues)

### 运行界面预览

以下截图由实际 QML 界面运行渲染，设备列表与能力参数使用示例数据。

| 浅色 | 深色 |
| --- | --- |
| ![浅色运行界面](example/moonlight-light.png) | ![深色运行界面](example/moonlight-dark.png) |

### 本次界面更新

- 蓝白浅色与简约黑灰深色主题，默认窗口为 1100 × 680；固定侧栏、设备列表与详情区支持窗口缩放。
- **连接桌面**直接启动或恢复主机提供的 `Desktop` 应用，独立于直启应用设置；其他应用正在运行时保留退出确认。未找到 `Desktop` 时提示在 Sunshine 中添加。
- **查看**打开完整应用列表，支持列表与网格切换；设备页保留配对、唤醒功能，**管理**提供重命名、删除、网络测试等入口。
- 显示当前串流配置，以及本机显示器的原生分辨率、该分辨率下的最高刷新率和硬件／软件解码检测状态。
- 设置按功能分类；统一按钮、弹窗、键盘焦点和连接状态界面，并补充简体／繁体中文翻译。

主题切换目前仅在当前运行期间生效。本机能力为检测结果，实际串流效果还取决于主机、编码配置与网络；界面缩放独立于串流分辨率。

### 下载 Desk

在 [GitHub Releases](https://github.com/coderleexl/moonlight-qt/releases) 下载已发布的安装包。三端由同一次 **Desk Build and Release** 工作流并行构建，均包含从固定源码编译的 Sunshine：

| 平台 | 安装包 | 适配范围 |
| --- | --- | --- |
| Windows | `Desk-Setup-x64.exe` 或 `Desk-x64.zip`，二选一 | Windows x64 |
| macOS | `Desk-macOS-arm64.dmg`，拖入 Applications | Apple Silicon；macOS 26 构建 |
| Linux | `desk_版本_amd64.deb` | Ubuntu 24.04 amd64 |

DEB 使用 `sudo apt install ./desk_版本_amd64.deb` 安装，以便同时安装依赖。其他 Debian/Ubuntu 版本需要单独验证。各版本实际可下载的平台以 Release 的 Assets 为准。

Windows 安装包未签名，macOS 使用临时签名且未进行 Apple 公证，系统可能要求确认打开。原版 Moonlight 的安装与设置不受影响；此前 Moonlight Desk Preview 的配置不会自动迁移到 Desk。

### 一次构建并自动发布

打开 [Desk Build and Release](https://github.com/coderleexl/moonlight-qt/actions/workflows/build-desk.yml)，点击 **Run workflow**。`publish` 默认开启：

1. Windows、macOS、Linux 三个 job 并行编译并打包。
2. 各平台执行主机进程管理、客户端及内置 Sunshine Web 启动检查；Windows 和 DEB 另做安装／卸载检查。
3. 发布 job 校验四个安装包均来自当前提交，并验证 SHA-256。
4. 创建 `desk-v版本` 标签，将 EXE、ZIP、DMG、DEB 和 `SHA256SUMS.txt` 上传到同一个 Release。
5. 上传完整后公开发布，再以未登录请求确认 Release 可访问。任何一步失败都会令工作流失败，不会把残缺包发布为正式版本。

发布版本取自 `app/version.txt`。发布新版本前先递增版本号；已发布标签不会被移动，已公开安装包不会被覆盖。同一提交的重试会校验已发布内容。也可以推送与版本号一致的 `desk-v*` 标签，自动触发同一流程。关闭 `publish` 则只在 Actions Artifacts 中保留测试包。

旧上游多平台构建保留为手动维护入口，不再与 Desk 重复自动运行。自动发布完全运行在 GitHub Actions 内，无需本机脚本、个人访问令牌或保持电脑在线。

### 内置 Sunshine 与局域网连接

Sunshine 固定版本为 `v2026.914.233613`（`63d35f7`），位于 `third_party/sunshine` 子模块。它作为 Desk 管理的子进程运行，退出 Desk 会停止它；不自动安装 Sunshine 系统服务或开机启动项。

1. 在“本机”页启动服务。默认“仅本机测试”只允许自己连接；另一台电脑连接时，先停止服务、关闭该选项，再启动。
2. 另一台设备添加 `主机局域网 IP:48989`。必要时允许内置 Sunshine 通过主机防火墙的对应网络。
3. 主机点击“配置与配对”，打开 `https://127.0.0.1:48990`，首次使用设置管理账号并登录。
4. 在客户端发起一次配对，保持 PIN 弹窗打开，在主机管理网页的 PIN 页面输入四位数字并提交。主机不会自动弹出 PIN 输入框。

`48989` 是连接端口，`48990` 是管理网页端口。当前关闭客户端 PIN 弹窗不会取消主机等待中的配对；若重复配对返回 409，可在主机端停止并重启内置服务后再试。局域网设备 ID 连接尚未实现。

macOS 需授予屏幕录制与辅助功能权限。Linux 包含 X11、Wayland/Portal 捕获；桌面门户可能要求确认共享，远程输入通过 uinput/uhid，并为活动桌面用户安装设备访问规则；权限尚未生效时注销并重新登录。Linux 不授予 `cap_sys_admin`，未启用直接 KMS 捕获与 NVFBC。

CI 验证安装与启动链路，不代替真实 GPU 串流、锁屏、UAC、手柄和显示权限测试。

### 本地开发与打包

Mac Apple Silicon 客户端开发（Qt 6.11，先准备上游 v17 macOS 依赖及客户端子模块）：

```sh
qmake -r moonlight-qt.pro CONFIG+=host_preview QMAKE_APPLE_DEVICE_ARCHS=arm64
make release -j8
open app/Desk.app
```

完整安装包使用对应平台的脚本，所需依赖见工作流：

- Windows：`scripts/build-preview-windows.ps1 -SunshinePayload ./build/sunshine-payload`。
- macOS：`bash scripts/build-desk-macos.sh`。
- Linux：`bash scripts/build-desk-deb.sh`。

先运行 `bash scripts/fetch-desk-source.sh windows|macos|linux` 中对应的一项以获取固定源码。Windows Sunshine 单独通过 MSYS2 执行 `scripts/build-sunshine-windows.sh`；macOS/Linux 打包脚本会同时编译 Sunshine。普通客户端编译不会自动包含主机服务，缺失时界面会禁用启动。

## 上游项目说明

下面保留上游的功能、下载与跨平台构建说明；其中的下载链接指向上游发行版。

[Moonlight PC](https://moonlight-stream.org) is an open source PC client for NVIDIA GameStream and [Sunshine](https://github.com/LizardByte/Sunshine).

Moonlight also has mobile versions for [Android](https://github.com/moonlight-stream/moonlight-android) and [iOS](https://github.com/moonlight-stream/moonlight-ios).

You can follow development on our [Discord server](https://moonlight-stream.org/discord) and help translate Moonlight into your language on [Weblate](https://hosted.weblate.org/projects/moonlight/moonlight-qt/).

 [![Build](https://img.shields.io/github/actions/workflow/status/moonlight-stream/moonlight-qt/build.yml?branch=master)](https://github.com/moonlight-stream/moonlight-qt/actions/workflows/build.yml?query=branch%3Amaster)
 [![Downloads](https://img.shields.io/github/downloads/moonlight-stream/moonlight-qt/total)](https://github.com/moonlight-stream/moonlight-qt/releases)
 [![Translation Status](https://hosted.weblate.org/widgets/moonlight/-/moonlight-qt/svg-badge.svg)](https://hosted.weblate.org/projects/moonlight/moonlight-qt/)

## Features
 - Hardware accelerated video decoding on Windows, Mac, and Linux
 - H.264, HEVC, and AV1 codec support (AV1 requires Sunshine and a supported host GPU)
 - YUV 4:4:4 support (Sunshine only)
 - HDR streaming support
 - 7.1 surround sound audio support
 - 10-point multitouch support (Sunshine only)
 - Gamepad support with force feedback and motion controls for up to 16 players
 - Support for both pointer capture (for games) and direct mouse control (for remote desktop)
 - Support for passing system-wide keyboard shortcuts like Alt+Tab to the host
 
## Downloads
- [Windows, macOS, and Steam Link](https://github.com/moonlight-stream/moonlight-qt/releases)
- [Snap (for Ubuntu-based Linux distros)](https://snapcraft.io/moonlight)
- [Flatpak (for other Linux distros)](https://flathub.org/apps/details/com.moonlight_stream.Moonlight)
- [AppImage](https://github.com/moonlight-stream/moonlight-qt/releases)
- [Raspberry Pi 4 and 5](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-Raspberry-Pi-4)
- [Generic ARM 32-bit and 64-bit Debian packages](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-ARM%E2%80%90based-Single-Board-Computers) (not for Raspberry Pi)
- [Experimental RISC-V Debian packages](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-RISC%E2%80%90V-Single-Board-Computers)
- [NVIDIA Jetson and Nintendo Switch (Ubuntu L4T)](https://github.com/moonlight-stream/moonlight-docs/wiki/Installing-Moonlight-Qt-on-Linux4Tegra-(L4T)-Ubuntu)

### Nightly Builds
- [Downloads](https://nightly.link/moonlight-stream/moonlight-qt/workflows/build/master)

#### Special Thanks

[![Hosted By: Cloudsmith](https://img.shields.io/badge/OSS%20hosting%20by-cloudsmith-blue?logo=cloudsmith&style=flat-square)](https://cloudsmith.com)

Hosting for Moonlight's Debian and L4T package repositories is graciously provided for free by [Cloudsmith](https://cloudsmith.com).

## Building

### Windows Build Requirements
* Qt 6.11 SDK or later (earlier versions may work but are not officially supported)
* [Visual Studio 2026](https://visualstudio.microsoft.com/downloads/) (Community edition is fine)
* Select **MSVC** option during Qt installation. MinGW is not supported.
* [7-Zip](https://www.7-zip.org/) (only if building installers for non-development PCs)
* Graphics Tools (only if running debug builds)
  * Install "Graphics Tools" in the Optional Features page of the Windows Settings app.
  * Alternatively, run `dism /online /add-capability /capabilityname:Tools.Graphics.DirectX~~~~0.0.1.0` and reboot.

### macOS Build Requirements
* Qt 6.11 SDK or later (earlier versions may work but are not officially supported)
* Xcode 15 or later (earlier versions may work but are not officially supported)
* [create-dmg](https://github.com/sindresorhus/create-dmg) (only if building DMGs for use on non-development Macs)

### Linux/Unix Build Requirements
* Qt 6 is recommended, but Qt 5.12 or later is also supported (replace `qmake6` with `qmake` when using Qt 5).
* GCC or Clang
* FFmpeg 4.0 or later
* Install the required packages:
  * Debian/Ubuntu:
    * Base Requirements: `libegl1-mesa-dev libgl1-mesa-dev libopus-dev libsdl2-dev libsdl2-ttf-dev libssl-dev libavcodec-dev libavformat-dev libswscale-dev libva-dev libvdpau-dev libxkbcommon-dev wayland-protocols libdrm-dev`
    * Qt 6 (Recommended): `qt6-base-dev qt6-declarative-dev libqt6svg6-dev qt6-wayland qml6-module-qtquick-controls qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtqml-workerscript qml6-module-qtquick-window qml6-module-qtquick`
    * Qt 5: `qtbase5-dev qt5-qmake qtdeclarative5-dev qtquickcontrols2-5-dev qml-module-qtquick-controls2 qml-module-qtquick-layouts qml-module-qtquick-window2 qml-module-qtquick2 qtwayland5`
  * RedHat/Fedora (RPM Fusion repo required):
    * Base Requirements: `openssl-devel SDL2-devel SDL2_ttf-devel ffmpeg-devel libva-devel libvdpau-devel opus-devel pulseaudio-libs-devel alsa-lib-devel libdrm-devel`
    * Qt 6 (Recommended): `qt6-qtsvg-devel qt6-qtdeclarative-devel`
    * Qt 5: `qt5-qtsvg-devel qt5-qtquickcontrols2-devel`
* Building the Vulkan renderer requires a `libplacebo-dev`/`libplacebo-devel` version of at least v7.349.0 and FFmpeg 6.1 or later.

### Steam Link Build Requirements
* [Steam Link SDK](https://github.com/ValveSoftware/steamlink-sdk) cloned on your build system
* STEAMLINK_SDK_PATH environment variable set to the Steam Link SDK path

**Steam Link Hardware Limitations**  
Moonlight builds for Steam Link are subject to hardware limitations of the Steam Link device:
* Maximum resolution: **1080p (1920x1080)**
* Maximum framerate: **60 FPS**
* Maximum video bitrate: **40 Mbps**
* **HDR streaming is not supported** on the original hardware

### Docker containers
If you want to use Docker for building, look at [this repo](https://github.com/cgutman/moonlight-packaging) containing canonical containers
for different architectures, which handle building deps and extra linking for you.

### Build Setup Steps
1. Install the latest Qt SDK (and optionally, the Qt Creator IDE) from https://www.qt.io/download
    * You can install Qt via Homebrew on macOS, but you will need to use `brew install qt --with-debug` to be able to create debug builds of Moonlight.
    * You may also use your Linux distro's package manager for the Qt SDK as long as the packages are Qt 5.12 or later.
    * This step is not required for building on Steam Link, because the Steam Link SDK includes Qt 5.14.
2. Download submodules and dependencies
    * Run `git submodule update --init --recursive` from within `moonlight-qt/`.
    * On Windows and macOS, you must also run `setup-deps.ps1` (Windows) or `setup-deps.py` (macOS).
    * Perform these steps each time you pull new changes from the Git repository.
3. Open the project in Qt Creator or build from qmake on the command line.
    * To build a binary for use on non-development machines, use the scripts in the `scripts` folder.
        * For Windows builds, use `scripts\build-arch.bat` and `scripts\generate-bundle.bat`. Execute these scripts from the root of the repository within a Qt command prompt. Ensure  7-Zip binary directory is on your `%PATH%`.
        * For macOS builds, use `scripts/generate-dmg.sh`. Execute this script from the root of the repository and ensure Qt's `bin` folder is in your `$PATH`.
        * For Steam Link builds, run `scripts/build-steamlink-app.sh` from the root of the repository.
    * To build from the command line for development use on macOS or Linux, run `qmake6 moonlight-qt.pro` then `make debug` or `make release`.
        * The final binary will be placed in `app/moonlight`.
    * To create an embedded build for a single-purpose device, use `qmake6 "CONFIG+=embedded" moonlight-qt.pro` and build normally.
        * This build will lack windowed mode, Discord/Help links, and other features that don't make sense on an embedded device.
        * For platforms with poor GPU performance, add `"CONFIG+=gpuslow"` to prefer direct KMSDRM rendering over GL/Vulkan renderers. Direct KMSDRM rendering can use dedicated YUV/RGB conversion and scaling hardware rather than slower GPU shaders for these operations.

## Contribute
1. Fork us
2. Write code
3. Send Pull Requests

Check out our [website](https://moonlight-stream.org) for project links and information.
