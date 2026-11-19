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
| 同目录 `music_library.h` | 有预算的迭代遍历和文件名匹配 |
| 同目录 `music_index_cache.h` | 双槽缓存、校验、世代选择、空闲扫描策略 |
| 同目录 `music_debug_command.h` | 有限调试命令语法和编号边界 |
| 同目录 `lichuang_dev_board.cc` | 初始化音乐工具，debug BOOT 长按 |
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
