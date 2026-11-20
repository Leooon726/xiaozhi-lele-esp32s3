# 测试交接：test-f2d91ff9dcdf-01

## 版本身份

- 2026-11-20，macOS arm64 / Mac Codex；本地候选，未发布 Release，未创建远端标签。
- 固件源码：f2d91ff9dcdf21d5769256976fbb1ef57fcb6d2a；树：3dcbadbe7a0e3332a6dcb244402d5554a9449334。
- 提交后干净工作区构建，ESP-IDF v6.1.1，原 Mac 独立工具链；构建 UTC 2026-11-20T15:10:16.741860+00:00。
- lckfb/szpi-esp32s3 / esp32s3 / lichuang-dev-lele-music-debug；en-US / custom:le le:乐乐。
- 组件锁 SHA-256：0d5dba0407c178c63f730b9ed06567605ce38fa87f0ef67934f71854efa6cd7d。
- 本地包 lele-f2d91ff9dcdf-01.zip，4889377 bytes，SHA-256 `382e1331197ca5ebf62adcd7a92c6a8c788f6b28a6b7a88afcea584dd032f4bb`。
- 本机忽略目录内包包含 flasher_args.json、manifest.json、SHA256SUMS、TEST_PLAN.md 与五文件。

| 地址 | 文件 | bytes | SHA-256 |
|---|---|---|---|
| `0x0` | `bootloader/bootloader.bin` | 16544 | `a3a29ecb6c04ef02322dfa98f0f778d2f1e2fcb310835bfaf49293107f213a99` |
| `0x8000` | `partition_table/partition-table.bin` | 3072 | `4811619cacae08ef2e0e71b7220c6033a346ca5da7ca179082408c963ef530b5` |
| `0xd000` | `ota_data_initial.bin` | 8192 | `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f` |
| `0x20000` | `xiaozhi.bin` | 3003920 | `2098b9f8391c952110b2c982a9637264d35976596d423275a9e9e5c8d33bf7c1` |
| `0x800000` | `generated_assets.bin` | 4166162 | `e3136998f504586b7058cdc0000e1033a8e916078f9e1e962d5318971e6436b3` |

## 变更与证据

- F-010：本板 CONFIG_LICHUANG_TF_MUSIC 音乐变体重写功耗接口，所有等级请求
  都交给 WifiBoard 的 PERFORMANCE。原版非音乐变体/其他板型不变，无新菜单。
- WifiBoard 该接口只映射 Wi-Fi modem sleep，不改 CPU 调频、屏幕亮度或音频解码。
- 旧 [test-582a94076992-01](test-582a94076992-01.md) idle 大文件失败保留；
  同版激活语音后明确 PERFORMANCE/NONE，64 KiB 1.939→0.929 秒，
  1 MiB 成功 6.762 秒。支持关闭休眠的重要性，不宣称控制了所有网络变量。
- 官方说明关闭 modem sleep 降低实时接收延迟、增加功耗：
  https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/wifi-driver/wifi-performance-and-power-save.html
- 本版完整 debug 构建/链接/容量/打包通过，应用 3003920 bytes；五分区/NVS 范围与摘要通过。
- 普通音乐、旧 debug 无 USB、stock 无音乐三个条件配置共 18 个翻译单元通过，非三个完整变体链接。
- 此次仅板级功耗映射，未新增镜像式单元测试，未重跑 8 项文件/HTTP套件或完整主机套件；旧测试不算本版实机通过。
- 当前物理设备仍为 582a940；本版未刷入。语音通道原本就会切性能，
  用户反馈的声音断续仍可能涉及网络/缓冲/解码，未证实被本策略解决。
- 增加功耗、电池续航及温升影响待实测，无未经测量的温度/续航数值。

## 实机计划

| ID / 功能 | 操作 | 预期 | 实际 | 状态 |
|---|---|---|---|---|
| P-01 / F-010 | 启动、联网回 idle，观察功耗日志 | LOW_POWER 请求后仍实际 PERFORMANCE/NONE | 未执行 | 未测 |
| P-02 / F-009、F-010 | 不唤醒助手，根目录唯一 1 MiB 文件 | 正常提交、字节和接收摘要匹配，记录耗时 | 未执行 | 未测 |
| P-03 / F-009、F-010 | 子目录 1 MiB、手机 MP3、结束传输 | 上传正常，安全关闭文件后恢复索引/点播 | 未执行 | 未测 |
| P-04 / F-010 | 语音回 idle、重连/重启、连续播报 | Wi-Fi 仍保持性能，无崩溃；音频断续独立记录 | 未执行 | 未测 |

接收流摘要不等于 SD 再读回校验；仅清理自身唯一诊断对象，不覆盖原文件。
旧失败保留，低功耗模式/电池体验不计为本版通过。

## 下一步

本地五文件包已准备；取得本次烧录确认后，使用 canonical device.py 先完整
备份及校验 16 MiB，再五文件写入和 NVS 保留检查。备份、凭据、设备标识、
原始日志、用户文件清单仅留 .local/。通过 0 / 失败 0 / 未测 4。
