<p align="center"><img src="assets/atrium-256.png" alt="H-Electric-glass Desktop" width="112"></p>

# H-Electric-glass Desktop

Windows 上的“液态玻璃”桌面：任务栏、开始菜单、通知中心、快捷设置、台前调度和桌面小组件，统一的玻璃材质与弹簧动效。

## 功能

- **任务栏**：玻璃材质、图标放大、窗口预览（按程序弹出）、托盘、状态图标、时钟。
- **开始菜单与搜索**：固定的程序、全部程序、文件夹，输入即搜索。
- **通知**：屏幕顶部居中弹出（也可选右上角或跟随任务栏）；微信、QQ、哔哩哔哩、飞书的消息和 Windows 通知都显示为同一种卡片。
- **语音输入**：任务栏上的麦克风，等同于微信输入法浮窗上的麦克风；录音时有指示点。
- **倒数日小组件**：自定义颜色、精确到时分秒、寄语、农历日期、到期提醒、置顶与排序；小、中、大三种尺寸，列表与大字两种样式。
- **其他小组件**：时钟、日历（含农历）、天气、电池、蓝牙、网络（网速与流量）、系统监视、便签、提醒事项、照片。
- **动效**：所有动画从静止开始时不跳帧；跟随 Windows 的“动画效果”设置（关闭时只保留淡入淡出）；可一键关闭 Windows 自带的窗口最小化、最大化动画。
- **界面语言**：简体中文或 English（设置 → 系统 → 语言）。
- **窗口切换**：Alt + Tab 打开玻璃切换面板。
- **维护**：日志在程序目录的 `logs\core.log`；把更新包放进程序目录的 `update\`，程序会自动安装并重启。

## 构建与发布

每次推送到 `main`，`.github/workflows/build.yml` 会在 Windows 上编译并打包：zip 发布在 Releases（`glass-<序号>`），同时保存在 `latest-build` 分支；编译日志在 `build-logs` 分支的 `latest.log`。

本地构建：`./tools/build.ps1 -Zip`（需要 Visual Studio C++ 工具、CMake 和 .NET SDK，版本见 `global.json`）。

## 说明

- 程序内部的文件名与数据目录仍沿用原名（`Atrium.exe`、`%LOCALAPPDATA%\Atrium`），以保证已有设置、开机启动和自动更新继续可用。
- 设计与协议文档：[docs/DESIGN.md](docs/DESIGN.md)、[docs/SETTINGS-PROTOCOL.md](docs/SETTINGS-PROTOCOL.md)。

## 许可与来源

本项目基于开源项目 [Atrium Desktop](https://github.com/wynter-yaxley/atrium-desktop) 2.6.0（MIT 许可证）独立发展。按 MIT 许可证的要求，原项目的版权声明保留在 [LICENSE](LICENSE) 中。
