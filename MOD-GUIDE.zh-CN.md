# 栏声 (Wavesill) 置于任务栏内容之下：安装与排错

[English](MOD-GUIDE.md)

模组让频谱真正位于 Windows 11 任务栏**内部**：开始按钮、程序图标、托盘和时钟之下，亚克力背景之上。仅 Windows 11，x64 与 ARM64。

## 1. 先跑起来栏声

启动 `Wavesill.exe` (或 `Wavesill-arm64.exe`)，0.6.2 或更新。托盘菜单 → 关于…，"Windhawk 模组"应显示"未检测到"，每条任务栏后面是 `[覆盖层]`。这是正常起点。

## 2. 安装模组

**从 Windhawk 仓库** (上架之后)：打开 Windhawk → Explore → 搜索 *Wavesill Behind Taskbar Content* → Install。

**从本仓库**：打开 Windhawk → Home → **Create a new mod**，会打开内置的源码编辑器和一段模板。把 `mod/wavesill-behind-taskbar-content.wh.cpp` 的全部内容替换进去，点 **Compile**。模组在本机编译，成功后自动启用。

## 3. 确认它在工作

大约五秒内：

- 原来浮在任务栏上的频谱消失，改为出现在图标后面。
- 栏声的"关于"里，"Windhawk 模组"显示 **已连接 (v0.6.3, Protocol 1)**，"模组阶段"是 **9/9 帧已送达**，每条任务栏后面是 `[模组]`。

如果没有：

1. 先看**模组阶段**那一行，它直接说明卡在第几步。九步依次是：XAML 已加载 → 诊断已接入 → 已订阅可视树 → 找到背景元素 → 已插入元素 → 定时器已建 → 已在跳动 → 已匹配任务栏 → 帧已送达。
2. 打开 Windhawk 里这个模组的页面 → Advanced → Debug logging，开着日志把模组禁用再启用。正常的日志依次是：
   ```
   Wavesill Behind Taskbar Content 0.6.3: init
   Bridge ready: 4 slots, ... bytes
   InitializeXamlDiagnosticsEx: 0x00000000
   AdviseVisualTreeChange: 0x00000000
   TaskbarBackground found (1 so far)
   Spectrum element inserted at index 1 of Windows.UI.Xaml.Controls.Grid (3 children)
   Timer started (queue yes, dispatcher no)
   First tick
   First frame copied: 1920 × 48 px, image 1920.0 × 48.0 DIP
   ```
3. 把阶段那一行和日志发到 [issue](https://github.com/GenuineHorace/Wavesill/issues)。

## 4. 如果 Explorer 崩溃了

模组自带崩溃保护：请求 XAML 加载自己时留下标记，稳定 20 秒后清除。若 Explorer 在此期间崩溃，下一次运行会跳过 XAML 部分、保持惰性，"关于"里显示"已跳过：上次接入时 Explorer 崩溃"。电脑可以正常用。想再试一次，就把模组禁用再启用。

## 5. 禁用、移除、更新

- 在 Windhawk 里禁用模组：栏声的覆盖层一秒内回来，不用重启任何东西。重新启用即唤醒。
- 移除：模组页面 → Remove。
- 被禁用的模组 DLL 会留在 Explorer 里直到 Explorer 重启 (XAML 诊断接口不会释放它)。无害，几乎不占内存。
- 更新：模组页面 → Advanced → Edit mod source，粘贴新文件，Compile。版本号在文件顶部的 `@version`，与栏声同步。

## 6. 它到底改了什么

只做两件事，不 hook 任何函数：

1. 通过 Windows 公开的 XAML 诊断接口 (Visual Studio 的"实时可视化树"、TranslucentTB、Taskbar Styler 用的同一个) 监听任务栏的 XAML 树，找到 `Taskbar.TaskbarBackground` 元素，在它后面插入一个 `Image`。
2. 每帧把栏声已经画好的图片从共享内存 `Local\Wavesill.Bridge` 拷进那个 `Image`。

样式、颜色、浓度、每显示器设置都是栏声的事，模组永远不用为它们改动。
