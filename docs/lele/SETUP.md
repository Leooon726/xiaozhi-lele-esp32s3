# 新机器 Setup：Windows / macOS / Linux

本文件是新环境的统一入口。Agent 先读根目录 `AGENTS.md` 和 `HANDOFF.md`，
再按本文件完成环境准备和构建验收。克隆仓库不包含 `.tools/` 工具链、
`.local/` 缓存或之前的构建产物，不要假设另一台机器的路径存在。

## 固定的构建输入

| 项目 | 当前乐乐基线 |
|---|---|
| 源码 | 先记录 `git rev-parse HEAD` 和 `git status --short`，保留用户改动 |
| SDK | ESP-IDF **v6.1.1**；复现时不用 `latest` 或滚动的 `release-v6.1` |
| Python | 容器内自带 Python 3.12；原生环境也使用 Python 3.12 |
| 芯片 / 板目录 | `esp32s3` / `lckfb/szpi-esp32s3` |
| 变体 | `lichuang-dev-lele-music-debug` |
| UI / 唤醒 | `en-US` / `custom:le le:乐乐`，MultiNet7 |
| 硬件 | 立创实战派 ESP32-S3，16 MiB Flash、8 MiB PSRAM |
| 组件快照 | `config/lele/dependencies.lock`，首次构建复制为根目录 `dependencies.lock` |

不要为解决 setup 改板名、引脚、唤醒词、语言或升级组件。已有根目录锁文件时
保留并核对来源；不要自动覆盖。新机器复现应从干净的新克隆开始。

## 推荐：统一用 Docker 编译

宿主机只需要 Git、Python 3.10+（仅运行标准库包装脚本）和能运行 Linux
容器的 Docker。SDK、Python 3.12、CMake、Ninja、交叉编译器和 SDK Python
依赖由官方 `espressif/idf:v6.1.1` 镜像提供，无需在宿主机逐个安装。
镜像按 Docker 主机架构选择，不要照搬旧构建服务的 `--platform linux/arm64`。

首次需要联网下载镜像和组件。为镜像、解压及构建预留约 25 GB 空间作为准备
预算（不是实测最低要求）；首次明显慢于后续增量构建。允许访问 Docker Hub、
GitHub 和 Espressif 组件服务；受限网络先处理代理/DNS，不要改源码绕过。

### 各系统前置准备

| 环境 | 准备和检查 |
|---|---|
| Windows PowerShell | 安装 Git、Python 和 [Docker Desktop](https://docs.docker.com/desktop/setup/install/windows-install/)，启用 Linux 容器（通常使用 WSL 2 后端），启动 Desktop；`py -3 --version`、`docker info` 均成功 |
| Windows WSL 2 | 启用 Docker Desktop 对该发行版的 WSL 集成，在 WSL 内安装 Git/Python，源码放 WSL 的 Linux 文件系统中；使用下面 Linux 命令，避免在 Windows/WSL 间共享同一个 build 目录 |
| macOS | 安装 Git、Python 3 和对应 Intel/Apple Silicon 的 [Docker Desktop](https://docs.docker.com/desktop/setup/install/mac-install/)，启动 Desktop，允许共享仓库所在目录；`python3 --version`、`docker info` 均成功 |
| Linux | 安装 Git、Python 3 和 [Docker Engine](https://docs.docker.com/engine/install/)（[Ubuntu 步骤](https://docs.docker.com/engine/install/ubuntu/)）；启动 daemon，并确保当前用户可运行 `docker info` |

Agent 复用已有安装；需要安装系统软件、调整虚拟化或 Docker 权限时，遵循当前
环境的授权流程。包装脚本不自动安装软件、修改 shell 配置或系统 Python。
Docker context 应连接本机；远程 daemon 无法直接挂载本机的源码路径。
路径建议简短、无逗号；Windows 示例使用 `C:\src\xiaozhi-lele-esp32s3`。

### macOS / Linux / WSL 2

```bash
git clone https://github.com/Leooon726/xiaozhi-lele-esp32s3.git
cd xiaozhi-lele-esp32s3
git status --short
python3 --version
docker info
python3 scripts/lele/build_docker.py
```

### Windows PowerShell

```powershell
git clone https://github.com/Leooon726/xiaozhi-lele-esp32s3.git
Set-Location xiaozhi-lele-esp32s3
git status --short
py -3 --version
docker info
py -3 scripts/lele/build_docker.py
if ($LASTEXITCODE -ne 0) { throw "Lele build failed" }
```

如果没有 `py` 但 `python --version` 是 Python 3.10+，用 `python` 替代 `py -3`。
此流程无需 Git Bash，无需执行 macOS 的 `env.sh`。包装脚本按自身位置找到仓库，
使用参数列表调用 Docker，自动准备缺失的锁文件、持久组件缓存和构建日志；
Unix 使用当前 UID/GID，Windows 使用容器默认用户。失败返回非零退出码。

以后在同一个 checkout 重跑同一命令即可增量构建。日志位于
`.local/logs/build-docker.log`（每次覆盖），工具数据在 `.local/`，均不进 Git。
本流程只挂载源码、编译和打包，不传递 USB 设备，不烧录。

## 备选：原生 ESP-IDF 环境

需要直接使用串口或不能运行 Docker 时，按 [ESP-IDF v6.1.1 官方安装指南](https://docs.espressif.com/projects/esp-idf/en/v6.1.1/esp32s3/get-started/index.html)
安装 SDK 和 ESP32-S3 工具。先按对应系统指南准备宿主机依赖；Python 使用
3.12。不要把工具链、venv 提交进仓库。

### macOS / Linux

在已安装 Python 3.12 和官方宿主机依赖的 shell 中：

```bash
git clone --branch v6.1.1 --recursive https://github.com/espressif/esp-idf.git "$HOME/esp/esp-idf-v6.1.1"
IDF_TOOLS_PATH="$HOME/esp/lele-idf-tools" bash "$HOME/esp/esp-idf-v6.1.1/install.sh" esp32s3
export LELE_IDF_PATH="$HOME/esp/esp-idf-v6.1.1"
export LELE_IDF_TOOLS_PATH="$HOME/esp/lele-idf-tools"
# 回到本仓库根目录再执行：
source scripts/lele/env.sh
python --version
idf.py --version
test -f dependencies.lock || cp config/lele/dependencies.lock dependencies.lock
bash scripts/lele/build.sh lichuang-dev-lele-music-debug
```

若机器有多个 Python 版本，先依据官方安装说明选择 Python 3.12，再运行安装；
用激活后的 `python --version` 验证结果，不以系统 `python3` 推断 SDK venv 版本。
`env.sh` 支持上面的路径覆盖，只影响当前 shell；其中 `.tools/python/...macos...`
是原 Mac 的可选路径，克隆后该目录不存在是正常的，不是 Linux 的安装步骤。
原 Mac 已有 `.tools/` 时可以直接复用，不必重装。

### Windows 原生

使用官方安装指南中的 ESP-IDF Installation Manager 安装 **v6.1.1** 和
ESP32-S3 工具，打开该安装提供的已激活 ESP-IDF 终端。确认
`python --version` 为 3.12、`idf.py --version` 为 v6.1.1。
不要在普通 PowerShell 中只设置 `IDF_PATH` 就认为环境已激活。

在已激活的 PowerShell、仓库根目录运行：

```powershell
if (!(Test-Path dependencies.lock)) { Copy-Item config/lele/dependencies.lock dependencies.lock }
python scripts/build.py lckfb/szpi-esp32s3 --name lichuang-dev-lele-music-debug --language en-US --wake-word 'custom:le le:乐乐' --zip
if ($LASTEXITCODE -ne 0) { throw "Lele build failed" }
```

这使用 canonical Python 入口，无需运行 Bash 包装脚本。原生构建与 Docker
构建的 CMake 缓存含不同绝对路径；切换构建方式或主机时优先新建 checkout，
不要复用旧机器的 `build/`、`managed_components/` 或本地工具目录。

## Setup 完成的验收标准

Agent 不能只确认 `idf.py --version` 就宣布 setup 完成，必须完成当前变体的
完整编译、链接和打包，并记录实际系统/架构、源码提交及 SDK 版本：

1. 构建命令退出码为 0，日志出现 `Project build complete` 和 ZIP 打包成功。
2. `build/flasher_args.json` 标明 `esp32s3`、16MB Flash；其中列出的五个文件
   都存在且非空：bootloader、partition table、OTA data、`xiaozhi.bin`、
   `generated_assets.bin`。
3. IDF 容量检查通过，应用不超过 `0x3f0000`、资源不超过 `0x800000` 字节。
4. `sdkconfig` 确认乐乐唤醒、音乐、debug 和 USB storage 均启用。
5. 校验 `releases/v2.6.0_lckfb-lichuang-dev-lele-music-debug.zip` 完整性，
   可用 `python3 -m zipfile -t <文件路径>`（Windows 换为 `py -3`）。
6. `git status --short` 中没有构建引入的受跟踪文件改动；报告任何跳过的验证。

完整分区地址、串口工具、备份及烧录流程见 [BUILD_FLASH.md](BUILD_FLASH.md)。
编译验收不等于实机验收；写入 Flash 或 OTA 前仍需当次授权和完整备份校验。
保留设备 NVS 时不能把 ZIP 内合并镜像直接从 0 地址写入。

## 常见阻塞

| 现象 | 处理 |
|---|---|
| `docker` 不存在 / 无法连接 daemon / socket permission denied | 先使当前终端 `docker info` 成功；启动 Docker 或按授权流程处理权限，不先改源码 |
| Windows 显示 Windows containers | 切换为 Linux containers，再重跑 |
| DNS、代理、组件下载失败 | 核对宿主机与容器网络；Docker 能拉镜像不代表容器能访问组件服务；修好网络后重跑以复用缓存 |
| `ESP-IDF missing` | Bash 原生入口需要 SDK 和路径覆盖；Docker 路径直接运行 `build_docker.py` |
| IDF 5.x / 组件要求 SDK 6.x | 切换到固定 v6.1.1 环境，不降级依赖 |
| CMake 报旧 `/project` 或另一台机器路径 | 使用新的 checkout 构建，不搬运 build 缓存；保留用户现有文件 |
| Docker bind mount 被拒 / 看不到源码 | 核对 Docker Desktop 共享目录、context 是否本机和仓库路径是否含逗号 |
| `getpwuid(): uid not found` | Unix UID 在镜像中没有用户名的提示；本次 Linux 构建已确认不阻断，HOME 已单独设置 |
| 主机单元测试缺依赖而 skip | 主机回归与固件构建是两套依赖；见 VALIDATION.md，不要把 skip 写成通过 |

## 已验证范围

- 2026-11-20：Ubuntu Linux aarch64，官方 `espressif/idf:v6.1.1`，
  Python 3.12.3，源码 `8e85347`，从新克隆完成依赖下载、完整编译/链接、
  合并和 ZIP 校验。应用 2,979,920 bytes，应用槽剩余 28%。
  随后用本次新增的正式 `build_docker.py` 入口重跑增量构建、链接及打包通过。
- 既有 macOS 原生 v6.1.1 完整构建与设备验证见 VALIDATION.md。
- Windows 原生、Windows Docker/WSL 2、macOS Docker 和 Linux x86_64
  流程尚未在本项目实测；上述提供操作步骤，不声称已通过跨平台验证。

Docker 使用方式依据 [Espressif 官方镜像说明](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/tools/idf-docker-image.html)。
SDK/组件版本以后若调整，需同步包装脚本、组件快照和本文件，并重新构建验收。
