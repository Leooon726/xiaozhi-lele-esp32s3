# 测试交接：test-d0b44f2d3f6c-01

本包 F-011：原创松鼠待机及六种表情。本包不含自动息屏。Ubuntu Linux aarch64 / Codex 开发，所有实机项目未测。

## 版本身份

| 字段 | 值 |
|---|---|
| 标签 / 发布状态 | test-d0b44f2d3f6c-01 / Pre-release，2026-11-20T22:02:05Z |
| Release URL | [下载页面](https://github.com/Leooon726/xiaozhi-lele-esp32s3/releases/tag/test-d0b44f2d3f6c-01) |
| 固件源码完整 SHA | d0b44f2d3f6ce065c8cc5fde36bac5ab5ecabd82 |
| 固件源码树 | 3123a706b39fa923612a4d854e4fce97cf3fb016 |
| 上一公开版 | [test-2920ecbd72d3-01](test-2920ecbd72d3-01.md)，保留旧记录 |
| 最近实机记录 | [test-f2d91ff9dcdf-01](test-f2d91ff9dcdf-01.md)，不能推断本包已刷入 |
| 板 / 芯片 / 变体 | lckfb/szpi-esp32s3 / esp32s3 / lichuang-dev-lele-music-debug |
| SDK / UI / 唤醒 | ESP-IDF v6.1.1 / 默认 LVGL / en-US / custom:le le:乐乐 |
| 镜像 / 构建时间 | espressif/idf:v6.1.1 / 2026-11-20T22:01:48.257856+00:00；镜像摘要见 manifest |
| 组件锁 SHA-256 | 0d5dba0407c178c63f730b9ed06567605ce38fa87f0ef67934f71854efa6cd7d |
| 构建来源 | 已提交干净源码重新构建，编译/链接/容量检查通过 |
| 测试包 / 大小 | lele-d0b44f2d3f6c-01.zip / 5034854 bytes |
| 包 SHA-256 | d8e34abb34e54168bbb18bd62a9e9a113b6f2c15cb3665fa6d63a1b738df29b1 |
| manifest | 包内 manifest.json；独立附件 lele-d0b44f2d3f6c-01-manifest.json |
| 下一执行者 | Mac Agent：下载校验、取得当次烧录授权后执行 Q-01..04 |

| 地址 | 文件 | bytes | SHA-256 |
|---|---|---|---|
| 0x0 | bootloader/bootloader.bin | 16544 | b53ab96f981cb7c3950f0a666ba2c4976c13307c20d7ca636aa5ae108d8910f4 |
| 0x8000 | partition_table/partition-table.bin | 3072 | 4811619cacae08ef2e0e71b7220c6033a346ca5da7ca179082408c963ef530b5 |
| 0xd000 | ota_data_initial.bin | 8192 | 7d2c7ac4888bfd75cd5f56e8d61f69595121183afc81556c876732fd3782c62f |
| 0x20000 | xiaozhi.bin | 3149840 | 41c23e1777a2c1ed35072189901c0c4f4b6d975895c03b44b8b5fada8f43e60f |
| 0x800000 | generated_assets.bin | 4166162 | 3295fd1f0673263862b73389d3cece7412b2daa22ecc5f3aa3862c3ca618daa9 |

包内包含五分区、flasher_args.json、manifest.json、SHA256SUMS 和 TEST_PLAN.md。
不包含合并镜像、私人备份、日志或设备标识；后续结果在 Git 追加，不替换附件。

## 变更与开发端验证

- 待机、开心、聆听、说话、思考、难过、困倦七张 144×144 透明 PNG，共 144630 bytes。
  只替换本板两个音乐变体默认 UI，不改变其他板、WeChat 或 Emote。
- 状态/云端情绪映射；启动 robot_2 也显示松鼠。重复图片源不重新设置，未知情绪保留当前画面。
  保留字幕、预览、触摸与方向功能。是静态状态切换，不是持续动画或口型同步。
- 完整主机 Ran 227 tests，OK (skipped=5)，无失败。跳过 cJSON/mbedTLS 两组、
  numpy/Pillow/packaging 两项、OpenSSL 开发库一项，不计为通过。
- 真实 LVGL 解码七张实际 PNG，生产状态/情绪方法和两方向布局/触摸回归通过。
  普通音乐、stock、WeChat、Emote 下五个翻译单元共 20 项语法检查通过，非四套完整链接。
- 固定提交 debug 构建、五分区/NVS 范围和包摘要检查通过。应用 3149840 bytes，槽余 24%。
  七张 PNG 原始字节在应用中逐一核对；合并 ZIP 内容与五分区一致。
- 发布后 Linux gh 下载回读四附件，逐字节、外部/内部摘要、manifest 引用核对通过；
  远端标签指向此源码，非草稿且标记 Pre-release。不是 Mac 下载、烧录或实机验收。
- 本轮未操作设备；音频断续、长期稳定性、PNG 实际内存余量待实测，不继承旧版结果。

## Mac 下载与烧录

先保留本地改动并同步 main，读 AGENTS.md、AGENT_WORKFLOW.md、SQUIRREL.md 和本记录。

~~~bash
gh release download test-d0b44f2d3f6c-01 \
  --repo Leooon726/xiaozhi-lele-esp32s3 \
  --pattern 'lele-d0b44f2d3f6c-01.zip' --pattern 'lele-d0b44f2d3f6c-01.zip.sha256' \
  --dir .local/firmware/test-d0b44f2d3f6c-01
cd .local/firmware/test-d0b44f2d3f6c-01
shasum -a 256 -c lele-d0b44f2d3f6c-01.zip.sha256
python3 -m zipfile -e lele-d0b44f2d3f6c-01.zip firmware
cd firmware
shasum -a 256 -c SHA256SUMS
~~~

回仓库根目录，独立 Python 环境安装 scripts/lele/requirements.txt。取得当次烧录授权，
按 BUILD_FLASH.md 执行 `python scripts/lele/device.py flash --build <firmware绝对目录> --confirm-write`。
先完整备份并设备端校验 16 MiB，核对分区，五文件写入并验证 NVS；多板指定 --port。
不匹配时停止，不整片擦除。本次发布不是烧录或回滚授权。日志/备份只留 .local/。

## 实机计划与首轮结果

| ID | 操作 | 预期 | 首轮实际 | 状态 | 证据/日期/执行者 |
|---|---|---|---|---|---|
| Q-01 | 启动后待机，分别横屏/竖屏、深色/浅色 | 松鼠透明边缘正确，位置合理，无旧机器人/emoji 跳回 | 未执行 | 未测 | 待 Mac 回填 |
| Q-02 | 唤醒、聆听、等待连接、播报、结束对话 | 表情随状态切换，字幕/预览图不失效，无卡顿或重启 | 未执行 | 未测 | 待 Mac 回填 |
| Q-03 | 在可触发的对话中检查开心/难过/思考/困倦等情绪 | 同一角色，图像切换位置稳定；无法触发的项保持未测 | 未执行 | 未测 | 待 Mac 回填 |
| Q-04 | 音乐播放、Songs、方向切换、Wi-Fi/USB 传输返回，连续操作 | 原有功能和触摸正常，返回后恢复松鼠；记录内存与音频表现 | 未执行 | 未测 | 待 Mac 回填 |

## 实机轮次 1：下载、备份与烧录（2026-11-21，Mac Agent）

- 用户明确要求同步云端、选择最新未验证 Pre-release 并烧录。Mac 干净工作区
  fast-forward 至 dbf7e40，查询 GitHub Releases，按发布时间确认本包为最新非草稿 Pre-release。
- 从该 Release 下载四附件；ZIP 与独立 SHA-256、包内 SHA256SUMS 全部通过；
  独立/包内 manifest 一致，标签源码、板型、唤醒词、芯片和五分区地址核对通过。
- canonical device.py 完整读取 16,777,216 bytes，设备端校验备份通过；
  五文件写入及校验通过，启动前 NVS 与备份一致，未整片擦除或迁移分区。
- 实际刷入源码 d0b44f2d3f6ce065c8cc5fde36bac5ab5ecabd82，app SHA-256
  `41c23e1777a2c1ed35072189901c0c4f4b6d975895c03b44b8b5fada8f43e60f`。
- 启动约 1.0 秒加载 346 首缓存 generation 36，1.4 秒全屏触摸区域
  (0,0)-(239,319) 就绪；4.6 秒进入 idle，实际 Wi-Fi PERFORMANCE/NONE。
- 约 20.9 秒完成后台扫描，仍为 346 首、cache saved=1；28.3/38.5 秒
  ready=1 / scanning=0 / idle。45 秒窗口未见 panic、循环重启或 PNG/内存分配错误；
  不能据此确认图像已正确显示。串口采集已停止并释放端口。
- 此为启动/串口证据，不代表松鼠显示、表情、声音或触摸物理验收；已请用户
  按 Q-01/02 观察。Q-01..04 继续保持未测，无法触发的情绪不补填通过。
- 私有 Flash/NVS、原始日志和设备信息仅保留本机忽略目录，不上传。

## 结论与下一步

通过 0 / 失败 0 / 未测 4 / 阻塞 0 / 不适用 0。Mac 已烧录并验证启动；等待 Q-01..04 实测回填。
本版不含自动息屏；下一项功能在独立源码提交实现，不会改变此标签与附件。
