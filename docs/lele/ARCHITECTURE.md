# 硬件和代码地图

## 硬件

| 项目 | 当前板配置 |
|---|---|
| 芯片/存储 | ESP32-S3，16 MiB quad Flash，8 MiB PSRAM |
| SD | SDMMC 1-bit，CLK GPIO47、CMD GPIO48、D0 GPIO21；FAT32 |
| 音频 | ES7210 输入、ES8311 输出、24 kHz mono 输出，PCA9557 控制功放 |
| 串口 | 原生 USB Serial/JTAG，VID:PID 303A:1001；实际 `/dev/cu.*` 编号会变化 |
| 唤醒 | MultiNet7 cn，`le le` → 乐乐，阈值 20% |
| 软件 | 小智 v2.6.0；ESP-IDF v6.1.1 commit `051e4c4eb20346e043cdd47bc388fde75ff8af63`；Python 3.12.16 |

板级原有音频/显示引脚以 `main/boards/lckfb/szpi-esp32s3/config.h` 为准，不能为了其他板直接改已有引脚。

## 路径地图

| 文件 | 职责 |
|---|---|
| `main/boards/lckfb/szpi-esp32s3/config.json` | 乐乐、音乐和 debug 构建变体 |
| `main/Kconfig.projbuild` | 音乐和调试功能开关 |
| `main/boards/lckfb/szpi-esp32s3/music_tools.cc` | SD 挂载、扫描任务、RAM 曲库、MCP、USB/按键测试入口 |
| 同目录 `music_limits.h` / `music_paths.h` | 1024 首预算、连续 PSRAM 路径表与可失败增长 |
| 同目录 `music_library.h` | 有预算的迭代遍历和无堆分配的文件名匹配 |
| 同目录 `music_index_cache.h` | 双槽缓存、校验、世代选择、空闲扫描策略 |
| 同目录 `music_debug_command.h` | 有限调试命令语法和编号边界 |
| 同目录 `lichuang_dev_board.cc` | 初始化音乐工具、功能键短按/长按隔离 |
| `main/application.*` | 主线程请求调度、会话关闭、音乐状态、结束恢复、自动 OTA 策略 |
| `main/notify/notify_player.*` | 本地 MP3/原有 HTTP Ogg 播放任务与取消/完成 |
| `main/audio/music_stream.*` | ID3 跳过、MP3 解码、混音、重采样、诊断 |
| `main/audio/audio_service.*` | 有界 PCM 队列、播放代号和取消 |
| `main/audio/sd_audio_path.h` | SD 路径范围、扩展名与非法路径验证 |
| `scripts/build.py` | 官方构建入口，乐乐自定义命令改用 mn7_cn |
| `scripts/lele/` | 独立环境、构建、备份烧录、本地调试和主机编译器适配 |

## 调度和内存约束

文件扫描为优先级 1，低于输入 8、输出 4、AFE 3、Opus 2。自动扫描只在连续空闲 10 秒后继续；已进入的单次 SD I/O 不能立即抢停。不是依赖“低 CPU 百分比”的硬保证，仍需语音时延实测。

MCP/按键/USB 都调用 Application 的请求接口，跨线程状态变更交给 Schedule。USB 播放要求 idle，MCP 可结束语音会话后排队；两者复用后半段播放器。PCM 队列有界，取消按 playback_id 隔离。

缓存先落盘到非活动槽，flush/fsync/关闭后读回验证；成功后更新 RAM，I/O 失败保留旧索引。缓存用内容校验防误损坏，不是加密或安全认证。挂载失败绝不格式化；不支持自动热插拔，更换卡应断电。

## 1024 首的内存预算（尚未实机验收）

每条路径最大 240 字节，固定记录含长度和终止符、对齐共 244 字节。路径表按
16/32/.../1024 项增长，在 ESP32 上仅使用 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT，
不会退回内部 SRAM。满表 249856 字节（244 KiB）；旧/新/缓存回读校验同时满表
为 749568 字节（732 KiB）。缓存加载按已校验的 count 一次分配，避免第三份表增长时叠加 realloc
临时缓冲；扫描增长期间两份表及 realloc 旧/新缓冲上界约 610 KiB。
这些数字仅是路径表预算，不是整机总内存，也不是可无条件分配成功的保证。

分配失败返回错误，缓存加载不修改既有输出；扫描失败保留旧索引/缓存。
扫描预算同步到 16384 个目录项，目录队列仍有独立 128 项/16 KiB 限制。
文件名匹配改用固定栈缓冲折叠 ASCII，避免在主线程遍历 1024 首时反复分配
短字符串；语义仍为包含匹配。MCP 每次结果仍最多 20 个/约 6 KB。

## USB SD 传输源码（已刷入，传输交互待验收）

`lele_menu.*` 管理顶层触摸模态菜单和输入事件，按钮/LVGL 回调只提交动作，
ESP timer 每 100 ms 至多排队一个主线程轮询。`music_storage.h` 给出扫描门禁和
卸载握手；`Application::SetMusicStorageBlocked` 防止异步排队的点播重新启动。
`usb_transfer.*` 管理 NVS 模式、仅 SD 的 TinyUSB MSC 启动与退出。采用重启
隔离两种存储所有者；传输启动分支位于板构造的屏幕/触摸初始化之后、摄像头/
音乐工具初始化之前，不启动音频模型。详细流程见 USB_TRANSFER.md。
