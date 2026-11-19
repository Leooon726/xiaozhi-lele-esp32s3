# 验证记录和边界

## 实机（2026-11-19）

| 验证 | 结果 |
|---|---|
| ESP32-S3 / 16 MiB Flash 识别 | 通过；设备标识仅保留本地 |
| 完整 Flash 读取与设备端校验 | 多次通过，备份不进 Git |
| 五文件烧录摘要及启动前 NVS 一致性 | 通过，未单独全片擦除 |
| 乐乐唤醒、正常问答 | 用户此前明确确认通过 |
| 整卡 MP3 扫描 | 找到 128 首，达到当前容量；不能保证全卡全部收录 |
| 索引缓存写入及下次启动加载 | 通过，多世代读写见本地日志 |
| USB status/play/stop | 通过，显式 VFS + 接收驱动版本 |
| 直接点播索引 0/1/2 | 三首均 44.1 kHz/16-bit/stereo 解码，状态 playing |
| 物理出声 | 索引 0 用户明确确认“能听到音乐”，随后反馈测试无问题 |
| 启动 underrun | 各首最初有 3–4 条，后续短时间未见持续；不宣称完全消除 |

未单独验收：BOOT 长按、播放中唤醒/停止后恢复完整语音、48 kHz 和其他编码变体、所有 128 首、长时间运行、换卡/坏卡/真实断电双槽恢复、WebSocket 与 MQTT/UDP 两种实机组合、自建 OTA。

曾有一次云端选曲后 DecodeMusic=false，后续直接点播三首未复现。没有证据说明已找到该次解码失败的唯一原因。

## 本地主机与编译

- 固件：ESP-IDF v6.1.1，ESP32-S3 debug 变体编译/链接/合并/ZIP 校验通过。
- 固件任务栈修复前的完整主机套件：204 项，4 项缺依赖跳过；6 项 CI 测试最初因 PATH 缺 `python` 出错，修正 PATH 后对应 15 项测试通过。不是固件错误。
- 音乐测试：调试入口开发后 15 项主机测试通过，包含曲库、缓存、流控制和命令语法（有效 0..127 与恶意输入）。DecodeMusic 的测试使用 codec/resampler/PSRAM 替身，不验证真实 MP3 音质。
- 关闭音乐功能的 7 个修改翻译单元曾编译通过，包含 WebSocket 与 MQTT/UDP；不是所有板/所有网络的完整构建或实机证明。
- 整理仓库后在独立目录重跑 210 项主机测试，通过，4 项因缺少 cJSON/mbedTLS 或 numpy/Pillow/packaging 跳过；其中新增的 5 项烧录计划边界测试通过（也单独重跑通过）。独立目录 ESP32-S3 完整构建、链接、合并镜像及 ZIP 已通过，本机日志留 `.local/`，不能上传。

## 重跑命令

```bash
source scripts/lele/env.sh
export CXX="$PWD/scripts/lele/host_cxx.py"  # Apple Clang 草案参数兼容
python -m unittest discover -s scripts/tests -v
bash scripts/lele/build.sh lichuang-dev-lele-music-debug
```

主机套件部分测试需要 cJSON/mbedTLS 或 Python numpy/Pillow/packaging；缺失应明示 skip。目标编译通过与用户听到声音是不同证据，分别记录。

本机旧产物和原始日志已迁移到 `.local/archive/outputs/`；只用于本地追溯，公开仓库不含固件转储、用户歌库、Wi-Fi 或账号配置。
