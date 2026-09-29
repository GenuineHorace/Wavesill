# 栏声桥接协议：栏声与 Windhawk 模组之间的约定

[English](BRIDGE.md)

`src/wavesill_bridge.h` 是唯一的权威定义；模组内含一份逐字副本，因为 Windhawk 模组只能是一个文件。本文是它的说明。

## 分工

- **栏声是"脑"**：采集、FFT、动态、样式、颜色、浓度、每显示器设置、主题，所有决定都在栏声，它每帧为每条任务栏渲染一张 premultiplied BGRA 位图。
- **模组是"哑显示器"**：在每条 Windows 11 任务栏的 XAML 树里、图标层之下、背景之上插一个 `Image`，把栏声的位图拷进去。它不解释任何数据。
- 结果：样式随便改，模组不用动；协议只搬像素，几乎不会变。

## 谁创建、谁打开

- **模组创建**共享内存 `Local\Wavesill.Bridge` (`CreateFileMappingW`，页面文件后备)，加载时创建、卸载时关闭。没装模组的用户不付任何代价。
- **栏声只打开** (`OpenFileMappingW`)，每秒试一次；打开后校验 `magic`、`protocol`、各尺寸与偏移都落在 `totalBytes` 内，否则视为不兼容、不碰它。

## 布局

```
[0, headerSize)                                    WsBridgeHeader (128 字节)
[headerSize, headerSize + slotCount × slotSize)    WsBridgeSlot[slotCount] (每个 144 字节)
之后                                               每个槽两块像素缓冲，各 bufferBytes 字节
```

模组在创建时填好头部的固定字段和每个槽的 `bufferOffset[2]` / `bufferBytes` (每块至少 1 MB；模组用 4 个槽、每块 3 MB，够 5120 × 150 px 的任务栏)。其它槽字段起始为零。

## 谁写什么

| 字段 | 写方 | 时机 |
|---|---|---|
| `header.magic / protocol / headerSize / slotSize / slotCount / slotBytes / totalBytes / modVersion` | 模组 | 创建时 |
| `header.modHeartbeatMs` | 模组 | 每 250 ms |
| `header.flags` | 模组 | 进度位 (`WS_STAGE_*`)，供栏声的"关于"窗口显示 |
| `header.appProtocol / appVersion` | 栏声 | 打开映射时 |
| `header.appHeartbeatMs` | 栏声 | 每帧 |
| `slot.taskbarHwnd` | 栏声 | 认领槽时写入，退出时清零 |
| `slot.x y width height stride dpi edge flags` | 栏声 | 每次更新；`flags` 携带 `WS_SLOT_PRIMARY` |
| `slot.front / frameSeq` | 栏声 | 一帧像素写完后翻转 (先像素，再内存屏障，再 `front` 与 `frameSeq`) |
| `slot.visible` | 栏声 | 每帧：0 表示什么都别显示 (静音已衰减到空、暂停、全屏程序在前、任务栏隐藏、该显示器被关闭) |
| `slot.appHeartbeatMs` | 栏声 | 每次更新 |
| `slot.modHeartbeatMs / modStatus` | 模组 | 每帧显示了该槽之后 (`modStatus`：0 空闲，1 正在显示，2 找不到任务栏，3 出错) |

时间戳都是 `GetTickCount64()` 毫秒，两个进程读同一个时钟。

## 心跳与降级

- 栏声：某槽的 `modHeartbeatMs` 在 500 ms 内且 `modStatus == 1` → 隐藏自己在那条任务栏上的覆盖层；否则显示覆盖层。`header.modHeartbeatMs` 超过 3 秒 → 关闭映射，之后每秒重试打开。
- 模组：某槽的 `appHeartbeatMs` 超过 500 ms 或 `visible == 0` → 隐藏元素；`header.appHeartbeatMs` 超过 3 秒 → 全部隐藏。
- 任何一方消失 (模组被禁用、Windhawk 卸载、Windows 更新弄坏模组、Explorer 重启、栏声退出)，用户最坏看到的就是另一方的行为：不会空白，不会双重绘制。

## 版本

- `WS_BRIDGE_PROTOCOL` 是两边之间唯一要对得上的数字，与两边的版本号无关 (那两个只用来显示)。
- 栏声只接受完全相同的协议号；模组只在 `header.appProtocol` 是它支持的值时显示。不匹配 = 什么都不做，绝不猜。
- 字段只增不改：`headerSize` / `slotSize` 让新旧结构体互相跳过对方不认识的尾部。`reserved` 必须为零。

## 任务栏与槽的配对

按窗口句柄：模组是从任务栏窗口 (`Shell_TrayWnd` 或 `Shell_SecondaryTrayWnd`) 找到它的 XAML 树的，栏声认领槽时把同一个句柄写进 `slot.taskbarHwnd`，于是每条任务栏正好拿到带自己句柄的那个槽。(0.7.0 之前的模组按宽、高、DPI 比较，并用 `WS_SLOT_PRIMARY` 区分镜像的任务栏；这个标志仍然会写。)

## 模组信任什么

映射的布局由模组自己确定并保存在自己的变量里，绝不从映射里读回 `headerSize`、`slotSize`、`slotCount`、`bufferOffset`、`bufferBytes`，因为同一用户的任何进程都能写映射。每帧只对 `width`、`height`、`stride`、`front` 取一次快照，并且只在 `stride ≥ width × 4` 且 `stride × (height − 1) + width × 4` 不超过缓冲区 (64 位算术) 时才拷贝。
