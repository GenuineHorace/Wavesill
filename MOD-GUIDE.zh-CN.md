# 栏声 (Wavesill) 置于任务栏内容之下：安装与排错

[English](MOD-GUIDE.md)

模组让频谱真正位于 Windows 11 任务栏**内部**：开始按钮、程序图标、托盘和时钟之下，亚克力背景之上。仅 Windows 11，x64 与 ARM64。

## 1. 先跑起来栏声

启动 `Wavesill-x64.exe` (或 `Wavesill-arm64.exe`)，0.6.2 或更新。托盘菜单 → 关于…，"Windhawk 模组"应显示"未检测到"，每条任务栏后面是 `[覆盖层]`。这是正常起点。

## 2. 安装模组

**从 Windhawk 仓库** (上架之后)：打开 Windhawk → Explore → 搜索 *Wavesill Behind Taskbar Content* → Install。

**从本仓库**：打开 Windhawk → Home → **Create a new mod**，会打开内置的源码编辑器和一段模板。把 `mod/wavesill-behind-taskbar-content.wh.cpp` 的全部内容替换进去，点 **Compile**。模组在本机编译，成功后自动启用。

第一次启用时，Windhawk 会为 `taskbar.dll` 下载几 MB 的 Windows 符号 (模组状态显示 "Loading symbols…")，最多要一分钟，并且需要联网；之后符号会缓存起来，直到 Windows 更新换掉这个文件。

## 3. 确认它在工作

大约五秒内：

- 原来浮在任务栏上的频谱消失，改为出现在图标后面。
- 栏声的"关于"里，"Windhawk 模组"显示 **已连接 (v0.7.0, Protocol 1)**，"模组阶段"是 **9/9 帧已送达**，每条任务栏后面是 `[模组]`。

如果没有：

1. 先看**模组阶段**那一行，它直接说明卡在第几步。九步依次是：符号已解析 → 已到达任务栏线程 → 定时器已建 → 已在跳动 → 找到 XAML 根 → 找到背景元素 → 已插入元素 → 已匹配任务栏 → 帧已送达。
2. 打开 Windhawk 里这个模组的页面 → Advanced → Debug logging，开着日志把模组禁用再启用。正常的日志依次是：
   ```
   Wavesill Behind Taskbar Content 0.7.0: init
   Bridge ready: 4 slots, ... bytes
   Timer started on the taskbar thread
   First tick
   Spectrum element inserted at index 1 of Windows.UI.Xaml.Controls.Grid in primary taskbar ... (1 taskbars)
   First frame copied: 1920 × 48 px, image 1920.0 × 48.0 DIP
   ```
3. 把阶段那一行和日志发到 [issue](https://github.com/GenuineHorace/Wavesill/issues)。

## 4. 如果模组加载失败

`taskbar.dll` 的符号解析不了时，Windhawk 会把模组标为失败：第一次使用时没联网，或者某次 Windows 更新改了名字。这期间栏声的覆盖层照常工作。检查网络后重试；如果是 Windows 更新之后一直失败，请在 issue 里报告 Windows 版本号。

## 5. 禁用、移除、更新

- 在 Windhawk 里禁用模组：元素从任务栏里取出，模组卸载，栏声的覆盖层一秒内回来，不用重启任何东西。
- 移除：模组页面 → Remove。
- 更新：模组页面 → Advanced → Edit mod source，粘贴新文件，Compile。版本号在文件顶部的 `@version`，只在模组本身有改动时才变，不必和栏声的版本号一致。

## 6. 它到底改了什么

只做两件事，不 hook 任何函数：

1. 通过几个 `taskbar.dll` 符号，从任务栏窗口找到它的 XAML 树 (和其它任务栏模组的做法一样)，在任务栏自己的线程上找到 `Taskbar.TaskbarBackground` 元素，在它后面插入一个 `Image`。
2. 每帧把栏声已经画好的图片从共享内存 `Local\Wavesill.Bridge` 拷进那个 `Image`。

样式、颜色、浓度、每显示器设置都是栏声的事，模组永远不用为它们改动。
