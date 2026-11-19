# macOS 构建、备份和烧录

## 独立环境

不修改系统 Python 或 shell 配置。推荐 Python 3.12、Git、CMake 3.31.10、Ninja 1.13.2。实测 SDK 为 **ESP-IDF v6.1.1**（最低要求 6.0.1；不支持 IDF 5.x）。

当前 Mac 已迁移了工具链到仓库下被忽略的 `.tools/`，可直接 `source scripts/lele/env.sh`。另一台电脑从 GitHub 克隆不会带工具链，按官方 SDK 安装流程在任意独立目录安装：

```bash
# 以下为新机器准备示例，由 agent 执行；没有刷写操作。
git clone --branch v6.1.1 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v6.1.1"
IDF_TOOLS_PATH="$HOME/esp/lele-idf-tools" bash "$HOME/esp/esp-idf-v6.1.1/install.sh" esp32s3
export LELE_IDF_PATH="$HOME/esp/esp-idf-v6.1.1"
export LELE_IDF_TOOLS_PATH="$HOME/esp/lele-idf-tools"
source scripts/lele/env.sh
python --version
idf.py --version
```

先确保 SDK 安装使用 Python 3.12；这些示例不自动修改 PATH/配置文件。`env.sh` 只影响当前 shell。也可在已经激活的官方环境中直接使用 `scripts/build.py`。

当前已验证构建的依赖快照为 `config/lele/dependencies.lock`。新环境第一次构建前可将其复制到根目录 `dependencies.lock`（被 Git 忽略），再由 IDF 解析；这是复现输入，不要手动改 managed_components。锁定的组件仍从官方组件服务下载。

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

它会先再次完整备份并验证，核对分区表与当前板一致，再写五个文件、验证 NVS 未变，最后启动设备。失败应先查看本地记录，不能立即 erase-flash。脚本首次整理后的写入分支尚未实际执行；原来已实测的五文件流程已写入它，下一次获授权刷写时验证此包装脚本。

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

烧录后读取启动日志，确认 mn7_cn / 乐乐、128 首缓存（或实际曲库数量）、正常 idle，无 panic/反复重启；再测试出声，见 DEBUGGING。不要只凭写入摘要宣布功能验收。
