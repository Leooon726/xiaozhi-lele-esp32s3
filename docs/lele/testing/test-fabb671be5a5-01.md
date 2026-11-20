# 测试交接：test-fabb671be5a5-01

## 版本身份

- 创建日期 / 执行端：2026-11-20，原 Mac / macOS arm64 Codex。
- 本地包标识：test-fabb671be5a5-01；**未发布 Release，未创建远端标签**，不能从 GitHub 下载此包。
- 固件源码 SHA：fabb671be5a5d5e15be08b67e8deb326c815326e；源码树：f1ca8af7b32083ca85e349d31c1c61eb07070316。
- 上版：[test-58f3361265bf-01](test-58f3361265bf-01.md)，W-02 上传失败保留。
- 板型：lckfb/szpi-esp32s3 / ESP32-S3 / lichuang-dev-lele-music-debug。
- SDK / UI / 唤醒：v6.1.1 / en-US / custom:le le:乐乐。
- 构建时间 UTC：2026-11-20T14:23:41.210476+00:00；提交后无受跟踪修改构建，无 Docker。
- 组件锁 SHA-256：0d5dba0407c178c63f730b9ed06567605ce38fa87f0ef67934f71854efa6cd7d。
- 本地包：lele-fabb671be5a5-01.zip，4888832 bytes；SHA-256：7f66794ef16582bb25427ab12bf93da6ea1999e58e13c7f1b6270810b47fa68c。
- 包含 flasher_args.json、manifest.json、SHA256SUMS、TEST_PLAN.md 和下列五文件，均在本机忽略的 .local/firmware 中。

| 地址 | 文件 | bytes | SHA-256 |
|---|---|---|---|
| `0x0` | `bootloader/bootloader.bin` | 16544 | `a3a29ecb6c04ef02322dfa98f0f778d2f1e2fcb310835bfaf49293107f213a99` |
| `0x8000` | `partition_table/partition-table.bin` | 3072 | `4811619cacae08ef2e0e71b7220c6033a346ca5da7ca179082408c963ef530b5` |
| `0xd000` | `ota_data_initial.bin` | 8192 | `7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f` |
| `0x20000` | `xiaozhi.bin` | 3003248 | `e42de30b3ea0218de68ee7016fb89035a49856de15d06b561a8ba8ba62d3c807` |
| `0x800000` | `generated_assets.bin` | 4166162 | `e3136998f504586b7058cdc0000e1033a8e916078f9e1e962d5318971e6436b3` |

## 修复与验证

F-009：上传前不再无条件 mkdir 已存在的父目录；stat 使用尾斜杠，使 FAT
挂载根目录映射为驱动中的 “/”。仅 ENOENT 才创建父目录，再验证其类型。
没有改写入/覆盖权限、隐藏文件保护、提交和恢复协议。

- 相关主机回归 8 项通过；FAT 根目录模拟在旧 header 可重现失败，新版通过。
- Debug 完整构建/链接/容量/打包通过；五分区范围和摘要通过，应用 3003248 bytes。
- 普通音乐、旧 debug 无 USB、stock 无音乐三个配置各六个翻译单元共 18 项通过，非完整变体链接。
- 本轮未运行完整 225 项套件，不能继承旧版套件结果。
- 已刷入并通过启动检查，此版根目录上传仍需真实 FAT 验收；旧版子目录大文件超时未证明根因，不宣称随此修复解决。

## 本版实机计划

| ID / 功能 | 操作 | 预期 | 实际 | 状态 |
|---|---|---|---|---|
| U-01 / F-009，原 W-02 | 新会话，根目录唯一名称的小文件 | 保存成功，大小/摘要一致 | 未执行 | 未测 |
| U-02 / F-009，原 W-02 | 同会话，根目录唯一名称 1 MiB 测试文件 | 保存成功，无 Fetch/创建文件错误 | 未执行 | 未测 |
| U-03 / F-009，原 W-02 | 子目录 1 MiB，记录耗时，手机约 1 MB MP3 | 分别记录响应、吞吐和接收摘要，超时另定位 | 用户上传报 500 Transfer interrupted or SD write failed；其它子项未测 | 失败 |
| U-04 / F-009 | 结束会话、重建索引与恢复点播 | 文件关闭后恢复，曲库更新，无崩溃 | 未执行 | 未测 |

不覆盖原文件。仅清理本次唯一诊断对象；完整 SD 读回/掉电专项未覆盖。
旧版后续长等待测试创建的诊断目录可能残留（会话关闭后清理请求未成功），
名称仅在本机私有记录；下次会话先核对并清理自身诊断对象。

## 烧录交接与当前设备

用户当次明确回复“烧录”后，已使用 canonical device.py 对本地五文件目录
操作。当前设备为 fabb671be5a5：完整 16 MiB 备份/设备校验、五文件写入
校验和启动前 NVS 保留均通过。没有使用旧 build 或整片擦除。
不使用旧 build 或 merged 镜像；不在此记录存放 Code、用户歌单、备份或原始日志。

## 实机轮次 1（2026-11-20，Mac 烧录与启动）

- 用户授权烧录；逐一复核包内 SHA256SUMS、manifest 与五文件大小/摘要。
- 完整 16 MiB 备份及设备校验、五分区写入与 NVS 一致性通过。
- 实际应用摘要与本版身份表一致，非凭 main 文档提交推断设备版本。
- 约 1 秒加载 340 首缓存，generation 26；SetupUI 后触摸区域为
  (0,0)–(239,319)，约 5 秒进入 idle。采集初段未见 panic 或循环复位。
- 21.2 秒后台重扫完成，仍为 340 首、cache saved=1；28.3 秒 ready=1 / scanning=0 / idle。
  索引 PSRAM 124928 bytes、剩余 1087488 bytes、最大连续块 983040 bytes。
- 已保持串口采集并请求用户开启新会话，以便独立测试根目录小文件与 1 MiB。
  此时上传 U-01..04 尚未执行；备份、设备标识和原始日志仅留本机 .local/。

## 实机轮次 2（2026-11-20，用户上传仍失败）

- 用户报告上传得到 “Operation stopped, 500 Transfer interrupted or SD write failed,
  file not saved”。已经通过 File::Open，错误来自接收/写入/摘要更新合并分支；
  实际上传目录未独立记录，不能当作 U-01 根目录完整验收通过。
- 同期串口在上传会话启动后记录 recv error 104，随后一次 recv error 11，
  紧接 URI handler execution failed；未见 panic。采集完成后串口已释放。
- SDK v6.1.1 的 httpd_txrx.c 将 EAGAIN/EINTR 映射为 HTTPD_SOCK_ERR_TIMEOUT；
  官方 file_serving 示例对此 continue 重试。当前代码将所有 received<=0 统一
  当作 HTTP 500，明确存在可恢复超时处理缺陷，不代表其它潜在原因都已排除。
- 下一修复加入有界重试（20 秒无进展、2 分钟总时限）并拆分接收/写卡/摘要错误，
  记录阶段及字节数，不记录路径、Code 或凭据。对应回归已通过，尚未刷入。

## 结论

通过 0 / 失败 1 / 未测 3；已烧录并启动，但 U-03 用户上传失败，需新修复版本复测，保留旧 W-02 失败。
