# 乐乐 · 立创实战派 ESP32-S3 小智语音助手

基于 [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) v2.6.0，适配立创·实战派 ESP32-S3。自定义唤醒词 **乐乐（lè lè）**，支持从 FAT32 SD 卡整卡索引、搜索和播放 MP3，并提供无需云端/唤醒词的本地串口播放调试入口。

这是一份完整固件源码仓库，不是 Flash 备份。上游原始介绍见 [README.upstream.md](README.upstream.md)，原有许可证保持不变。

## 接手从这里开始

1. **[交接与当前状态](docs/lele/HANDOFF.md)**：已经做了什么、哪些已实机验证、下一步和不能据此推断的结论。
2. **[构建、备份和烧录](docs/lele/BUILD_FLASH.md)**：macOS 独立环境、构建命令、分区地址、备份和回滚。
3. **[本地调试](docs/lele/DEBUGGING.md)**：USB 命令、BOOT 长按、日志阶段与排查方法。
4. **[硬件与设计](docs/lele/ARCHITECTURE.md)**：引脚、模块位置、内存与状态调度、SD 索引缓存。
5. **[验证记录](docs/lele/VALIDATION.md)**、**[开发历史](docs/lele/HISTORY.md)**、**[后续 OTA](docs/lele/OTA.md)**。
6. Agent 开始工作前先读 [AGENTS.md](AGENTS.md)，无需本聊天历史。

## 曲库扩容与新菜单

曲库上限从 128 提高至 1024，路径表移到连续 PSRAM 缓冲；满容量约 244 KiB，
刷新校验时路径表峰值约 732 KiB。内存不足保留旧曲库，不回退挤占内部 SRAM。
旧 SD 缓存仍可复用；搜索返回仍最多 20 首。扩容版已刷入，实测扫描、保存并重启
加载 340 首；1024 首满容量和语音并行仍需专项验收。

新源码增加功能键长按 2 秒菜单、USB SD 文件传输模式，**已刷入，菜单/传输交互待实机验收**。
在菜单选择传输后重启进入磁盘模式，Finder 推出后返回助手并重建索引；
最新源码已修复传输模式跨重启残留，RESET/重新上电默认回助手；**此修复已刷入，待实机验收**。
长按菜单新增 Display，可切换浅色/深色并调节、记住背光亮度；**显示设置已刷入，用户已确认切换、调亮度和重启记忆正常**。
操作见 [显示设置](docs/lele/DISPLAY.md) 和 [USB_TRANSFER.md](docs/lele/USB_TRANSFER.md)。

触屏助手点击初版 457e126 的三项实测失败；已定位并修复点击区域创建过早被界面遮挡的问题
（**修复版未刷入**）。用户已要求改为普通助手界面全屏轻点唤醒、打断播报或结束监听，
手势及验收边界见 [TOUCH.md](docs/lele/TOUCH.md)。

## 当前实测基线

2026-11-19：设备已运行 `lichuang-dev-lele-music-debug`。乐乐唤醒及对话此前已实测；SD 曲库扩容后缓存加载 340 首；USB status/play/stop 已实测，直接点播前三首正常解码，用户确认听到音乐。随后用户反馈测试没有问题。

这不等于所有歌曲、所有采样率和所有交互组合都已验证：之前一次云端选曲后的解码失败尚未明确复现根因；BOOT 长按、长时间稳定性和自建 OTA 仍见验证清单。

## 快速构建与本地调试

准备 ESP-IDF v6.1.1 / Python 3.12 环境，详细步骤见构建文档：

```bash
source scripts/lele/env.sh
bash scripts/lele/build.sh lichuang-dev-lele-music-debug
python scripts/lele/music_debug.py status
python scripts/lele/music_debug.py play 0 --seconds 20
python scripts/lele/music_debug.py stop
```

调试脚本需要已刷入 debug 变体；它会直接控制现有播放器。打开原生 USB 串口可能使设备重启，脚本会等待启动及索引就绪。主机结束采集不会自动停止音乐。

| 变体 | 用途 |
|---|---|
| `lichuang-dev` | 原有板级变体 |
| `lichuang-dev-lele` | 乐乐唤醒，对照版本 |
| `lichuang-dev-lele-music` | 乐乐 + SD MP3，无本地调试入口 |
| `lichuang-dev-lele-music-debug` | USB 命令调试；新源码长按调菜单（菜单待实机） |

## 文件与隐私

源码、文档、通用脚本和构建依赖锁文件进入 Git。工具链、构建产物、完整 Flash/NVS 备份、设备标识和原始串口日志只放在被忽略的 `.tools/`、`.local/` 中。不要对这些目录使用 `git add -f`。

烧录或擦除前必须获得用户当次授权，并先完成完整 Flash 备份及设备端校验。自动更新官方固件在音乐变体中已禁用，避免覆盖自定义功能；自建 OTA 尚未完成。

## 来源

上游基线提交：`4de00aed740595a8713b01f8332b9b2cf30685bc`。
音乐功能参考 MIT 项目 [jianglixi-oss/xiaozhi-nas-tf-music](https://github.com/jianglixi-oss/xiaozhi-nas-tf-music)，提交 `3ede3d71362ade67387792334a6c17ace298220d`；仅移植 SD MP3 路径，未引入 NAS/AAC 等功能。许可见 [第三方许可](third_party/licenses/xiaozhi-nas-tf-music.txt)。
