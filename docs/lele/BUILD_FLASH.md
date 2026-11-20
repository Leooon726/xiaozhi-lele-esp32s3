# 构建、备份和烧录

远程开发、本地烧录时，按 [AGENT_WORKFLOW.md](AGENT_WORKFLOW.md) 交接，
从 [TESTING.md](TESTING.md) 选择明确测试版本。下载包需包含五分区文件及
`flasher_args.json`，不能把当前 `build.py --zip` 的合并镜像 ZIP 当作保留
NVS 的五文件测试包。每次实际刷入及验收结果写入对应版本记录。

## 新机器环境入口

Windows / macOS / Linux 的完整准备步骤、固定版本、Docker 统一入口、原生
安装、常见阻塞及验收标准统一维护在 **[SETUP.md](SETUP.md)**。
新机器优先使用 `python3 scripts/lele/build_docker.py`（Windows：`py -3`），
无需复制原 Mac 工具目录。实测 SDK 为 **ESP-IDF v6.1.1**，不支持 IDF 5.x。

原 Mac 的 `.tools/` 环境仍可用 `source scripts/lele/env.sh` 激活；它不是
克隆后自带的环境。组件快照为 `config/lele/dependencies.lock`，Docker 包装
脚本只在根目录锁缺失时复制；已有环境不自动覆盖。不手动改 managed_components。

## 构建

```bash
bash scripts/lele/build.sh lichuang-dev-lele-music-debug
# 等价的官方入口：
python scripts/build.py lckfb/szpi-esp32s3 \
  --name lichuang-dev-lele-music-debug --language en-US \
  --wake-word 'custom:le le:乐乐' --zip
```

非调试音乐版可改用 `lichuang-dev-lele-music`。名称会影响设备标识及后续 OTA 兼容性。构建脚本会更新本地 sdkconfig/build；切换目标后必须重新核对。

产物：`build/flasher_args.json`、`build/flash_args`、五个分区文件、`build/merged-binary.bin`，以及 `releases/v2.6.0_lckfb-lichuang-dev-lele-music-debug.zip`。这些均不进 Git。

## 工具与端口

已激活 IDF 环境内可使用 esptool 5.6.0 和 pyserial 3.5。也可为纯串口工具独立建 venv：

```bash
python3.12 -m venv .venv
.venv/bin/python -m pip install -r scripts/lele/requirements.txt
.venv/bin/python scripts/lele/device.py ports
```

脚本默认选唯一的 303A:1001 USB 设备。多板连接时必须显式 `--port /dev/cu.usbmodem...`；不能仅凭端口名称推断型号。esptool 使用 `--chip esp32s3` 并在操作中识别芯片与实际 Flash。

## 备份与烧录

**Agent 每次刷写/擦除前必须获得当次用户确认。编译成功不是烧录授权。**

只备份（不写 Flash）：

```bash
python scripts/lele/device.py backup
```

脚本读取完整 16 MiB，使用设备端 verify-flash 验证，记录 SHA-256，并单独保存 NVS 切片。备份结束保持下载模式，按 RESET 恢复启动。BOOT/RESET/重新插线如需用户协助，应明确说明。

以下命令只允许在得到用户刷写确认后运行：

```bash
python scripts/lele/device.py flash --build build --confirm-write
```

它会先再次完整备份并验证，核对分区表与当前板一致，再写五个文件、验证 NVS 未变，最后启动设备。失败应先查看本地记录，不能立即 erase-flash。2026-11-19 扩容版刷写已实测此包装脚本：完整备份/设备端校验、五文件写入、NVS 保留和重启均通过。

| 地址 | 文件（以 flasher_args.json 为准） | 说明 |
|---|---|---|
| `0x0` | `bootloader/bootloader.bin` | 引导程序 |
| `0x8000` | `partition_table/partition-table.bin` | 分区表 |
| `0xd000` | `ota_data_initial.bin` | OTA 选择数据 |
| `0x20000` | `xiaozhi.bin` | 应用，槽上限 `0x3f0000` |
| `0x800000` | `generated_assets.bin` | 资源，上限 `0x800000` |

NVS 为 `0x9000..0xcfff`；脚本同时按擦除扇区边界检查，拒绝覆盖。保留 NVS 时不能直接从 0 地址写整合并镜像，也不要用整片擦除。

新脚本的所有记录在 `.local/backups/<时间>/`：完整备份、NVS、写入 manifest、校验结果、私有 esptool 日志。不要上传或对这些目录强制 git add。

## 回滚

使用对应完整备份恢复将覆盖当前所有 Flash/NVS；同样必须先获用户确认、再次备份当前状态，并核对芯片、16 MiB 容量、文件长度和 SHA-256。仅应用回滚和恢复完整 Flash 的语义不同，应先选择恢复目标。此仓库不自动执行回滚或 erase-flash。

烧录后读取启动日志，确认 mn7_cn / 乐乐、实际曲库缓存（最近扩容实测 340 首）、正常 idle，无 panic/反复重启；再测试出声，见 DEBUGGING。不要只凭写入摘要宣布功能验收。
