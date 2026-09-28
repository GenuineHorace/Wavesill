# 栏声 · Wavesill

**融在任务栏里、不挡操作的淡淡频谱。**

[English](README.md) · [Windhawk 模组](#windhawk-模组-可选windows-11)

栏声把 Windows 此刻正在播放的声音 (音乐、视频、通知音……什么都行) 实时变成一条很浅的频谱，铺满整个任务栏。它完全点击穿透，跟随任务栏的位置、高度、DPI 与主题，安静时自动消失。单文件 C++/Win32，不依赖任何运行库，约 500 KB。

## 运行

1. 从 [Releases](https://github.com/GenuineHorace/Wavesill/releases) 下载与你机器架构一致的 exe：`Wavesill.exe` (x64) 或 `Wavesill-arm64.exe` (ARM64)。Windows 10 1703+ 或 Windows 11。没有安装程序，不依赖任何运行库。 (拿错也能跑，走的是模拟层；"关于"窗口的"构建"一行会告诉你当前架构。)
2. 托盘会出现一个小图标。播放任何声音，任务栏上就会出现淡淡的频谱。
3. 右键 (或左键) 托盘图标：

   | 菜单项 | 说明 |
   |---|---|
   | **样式** | 柱条 / 填充 / 曲线，以及一个独立的"向边缘渐淡"开关 (三种样式都可以叠加) |
   | **颜色** | 跟随主题 (默认：深色任务栏白、浅色任务栏黑) / 白色 / 黑色 |
   | **浓度** | 4% / 7% / 10% (默认) / 14% / 20% / 30% |
   | **下落** | 即时 / 峰值保持 (顶部标记停 0.3 秒再带重力落下) |
   | **各显示器** | 只在有两条以上任务栏时出现：每条任务栏可单独设置样式、颜色、浓度、下落，或不在该任务栏上显示 |
   | **自定义托盘图标** | 只在 `TrayIcons` 文件夹里有 .ico 时出现 (见下) |
   | **暂停** / **开机自动启动** | |
   | **打开数据文件夹** | 打开 `%LOCALAPPDATA%\Wavesill` |
   | **关于…** | 版本、构建、音频设备与采样率、主题、托盘图标来源、Windhawk 模组状态与阶段、每条任务栏的尺寸 / 位置 / 当前由谁显示 |
   | **退出** | |

   托盘图标、菜单、"关于"窗口都跟随系统深浅色；Windows 11 22H2+ 上"关于"用 Mica 背景。

首次运行未签名的 exe 时 SmartScreen 可能会拦一下，点"更多信息 → 仍要运行"即可；或者自己编译 (见下)。

## 数据文件夹

所有数据都在 `%LOCALAPPDATA%\Wavesill\`：

```
settings.ini        全部设置。人可读；栏声未运行时可手工编辑；删掉即恢复默认。
TrayIcons\          自定义托盘图标 (可选)
  tray_dark.ico       深色任务栏时使用
  tray_light.ico      浅色任务栏时使用
  tray.ico            对应文件缺失时两种主题共用
  README.txt          同样的说明
```

删掉这个文件夹并取消"开机自动启动"就是干净卸载。"开机自动启动"是唯一写注册表的地方 (`HKCU\Software\Microsoft\Windows\CurrentVersion\Run\Wavesill`)，因为 Windows 只认那里。

有一个设置没有菜单项：`BorderInset`。Windows 11 的任务栏顶部有一条 1 DIP 的边框线，默认 (`auto`) 在 Windows 11 上把频谱区域缩进 1 px (按 DPI 缩放)，Windows 10 上不缩进。改成一个 96 DPI 下的像素数即可覆盖。

## 它会自动处理的事

| 情况 | 行为 |
|---|---|
| 任务栏在底 / 顶 / 左 / 右 | 从贴屏幕边的那一侧长出柱条 (竖直任务栏时横向生长) |
| 多显示器、多条任务栏 | 每条任务栏各一层 (`Shell_TrayWnd` 和每个 `Shell_SecondaryTrayWnd`) |
| 分辨率 / DPI 切换、任务栏改高度、移动到别的边 | 每帧读取任务栏真实矩形与 DPI，不会错位 |
| 任务栏自动隐藏 | 随任务栏滑出、滑回 |
| 全屏游戏 / 视频 / 演示 | 该显示器上有全屏窗口时隐藏 |
| 深色 / 浅色主题 | 图标、菜单、关于窗口一起切换；频谱按"颜色"设置 |
| 任务栏爬到最上层 | 定期检查 z 序并恢复 |
| 切换 / 拔掉 / 重置默认输出设备 | 采集线程自动重新打开新的默认设备 |
| Explorer 重启 | 重新找任务栏、重建覆盖层、重挂托盘图标 |
| 完全安静 | 柱条衰减到 0 后停止绘制 (几乎零占用) |
| 声音很轻 | 慢速自动增益：轻的通知音也看得见，大动态的音乐也不会顶死 |
| 重复运行 | 单实例，第二次启动静默退出 |
| Windhawk 模组在运行 | 见下 |

## Windhawk 模组 (可选，Windows 11)

覆盖层再怎么做也是"浮在任务栏上面"。想让频谱真正位于任务栏图标之下、亚克力背景之上，必须在 Explorer 里动手，那是 [Windhawk](https://windhawk.net) 模组的活。栏声本体永远不注入任何进程；它只是把每帧画好的图片放进一小块共享内存 (`Local\Wavesill.Bridge`)。模组在时由它在任务栏内部显示；模组不在、被禁用或被 Windows 更新弄坏时，覆盖层在一秒内自动接管。

模组是 `mod/wavesill-behind-taskbar-content.wh.cpp`，也以 **Wavesill Behind Taskbar Content** (中文名"栏声 (Wavesill) 置于任务栏内容之下") 的名字发布在 Windhawk 模组仓库。安装与排错见 **[MOD-GUIDE.zh-CN.md](MOD-GUIDE.zh-CN.md)**；两边的协议见 [BRIDGE.zh-CN.md](BRIDGE.zh-CN.md) 与 `src/wavesill_bridge.h`。仅 Windows 11，x64 与 ARM64。

## 视觉

- 频段：35 Hz ～ 16 kHz 对数分布，每 4 px (按 DPI 缩放) 一个频段；柱条样式下柱宽 3 px、间隔 1 px。
- **柱条**：顶端一行略亮。**填充**：连续实心区域。**曲线**：1.5 px 抗锯齿的平滑上边界。**向边缘渐淡**：柱条 / 填充区域 / 曲线下方从顶端向任务栏边缘渐淡到 30%。
- 上升快 (30 ms)、下落慢 (140 ms)，相邻频段轻微平滑 (曲线样式平滑两遍)。
- **峰值保持**：顶部标记停 0.3 秒后以 3 倍屏高/s² 的加速度落下。
- 250 Hz 以上有 4 dB/oct 的抬升：音乐的高频能量天然少得多，不抬的话右半边永远是平的。

这些参数都在 `src/wavesill.cpp` 顶部的 `namespace cfg` 里。

## 编译

一个 `.cpp`、一个 `.rc`、两个头文件，不依赖第三方库。

```
build.cmd                                Visual Studio (x64 Native Tools Command Prompt)
./build.sh                               Zig，任何平台交叉编译：pip install ziglang
TARGET=aarch64-windows-gnu ./build.sh    Zig 编 ARM64
build-zig.cmd                            Windows 上用 Zig
./build-release.sh                       两个架构一起编到 dist/
tools/make_icons.py                      重新生成图标 (Python 3 + Pillow)
```

发布版用 Zig 编出来：静态链接 libc++，只依赖系统 DLL。GitHub Actions 每次推送都会编两个架构；推 `v*` 标签时自动附到 Release 上。

## 已知限制

- WASAPI loopback 抓不到独占模式 (ASIO / WASAPI Exclusive) 输出的声音。
- 深色菜单用的是 uxtheme 未公开的导出 (135/136/104)；将来 Windows 若改掉，菜单会退回浅色，其它功能不受影响。

MIT License
