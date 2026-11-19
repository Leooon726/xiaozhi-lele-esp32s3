# USB 管理 SD 卡：实现调研

2026-11-19 初始调研记录；后续用户授权实现的源码与状态见 [USB_TRANSFER.md](USB_TRANSFER.md)。本页保留方案依据，不作为功能验收。
对应 TODO.md 第 3 项。结论：采用官方 TinyUSB MSC + SDMMC；建议首版使用
需要重启的独立文件传输模式，后续再考虑运行时切换和 CDC 复合设备。

## 已确认的依据与未确认项

- [立创板级介绍](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/introduction.html)
  说明 Type-C 经 CH334F Hub 分成 CH340K/UART0 与 ESP32-S3 USB-OTG 两路。
  因此根据官方设计无需另接读卡器或飞线。当前 Mac 端口枚举只见原生
  Serial/JTAG，没有 CH340 串口；桥接驱动/枚举需单独确认，不能当作已有可用通道。
- [ESP-IDF v6.1.1 官方 tusb_msc 示例](https://github.com/espressif/esp-idf/blob/v6.1.1/examples/peripherals/usb/device/tusb_msc/README.md)
  与本地 SDK 一致，支持 SDMMC 和 1-bit 模式。示例组件依赖为
  `espressif/esp_tinyusb: ^2.0.1~1`。实现时解析并锁定具体组件版本，不能混用
  旧版 `tinyusb_msc_storage_init_sdmmc` 与新版 `tinyusb_msc_new_storage_sdmmc` API。
- 本板现有 SDMMC 是 CLK47 / CMD48 / D0=21、1-bit，需沿用；官方示例默认
  GPIO 与 4-bit 配置不能照搬。工程目前没有声明 esp_tinyusb 依赖。
- [ESP-USB 文档](https://docs.espressif.com/projects/esp-usb/en/latest/esp32s3/usb_device.html)
  说明 OTG 与 Serial/JTAG 共享内部 PHY，以及应用/USB 存储所有权切换接口。
  USB Full Speed 不是 USB High Speed；实际复制速度需要在本板测量。
- [Serial/JTAG 文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-guides/usb-serial-jtag-console.html)
  明确该硬件接口只能串口/JTAG，不能直接增加磁盘功能。

## 建议首版：重启进入文件传输模式

1. 显式请求“USB 文件传输”，在主任务中停止音乐并等待解码任务关闭文件，
   阻止新点播、扫描和缓存写入；超时拒绝切换，不能只发送 stop 就立即卸载。
2. 保存启动模式并重启。传输启动分支不初始化语音模型、音乐索引工作线程或
   原生 USB Serial/JTAG 调试任务。只保留必要屏幕/按键、SDMMC 和 TinyUSB MSC。
   这样首次实现不必在运行中的语音任务之间迁移 USB 控制器和文件系统。
3. SD 交由 TinyUSB 存储模块统一管理，导出整张卡为 MSC LUN。Mac Finder 直接
   浏览、复制和改名；不需要自制文件传输协议。不得导出内部 Flash/NVS。
4. Mac 安全推出后确认 MSC eject/所有权事件和未完成 I/O，再允许设备端退出。
   退出重启回助手，重新挂载 SD，安排立即更新索引，去掉新增/改名/删除造成的
   旧路径；刷新未完成时点播要检查真实文件，不能把旧缓存当作最新目录。
5. 插着 USB 正常运行时保持助手模式，不自动把卡交给 Mac。USB 供电时拔线会
   断电，正常流程使用 Finder“推出”，不要求物理拔线。

持久模式标志需要明确恢复策略：传输时意外复位不能在 Mac 仍持有磁盘时自动
启动助手并挂载卡。恢复入口需屏幕/按键可操作，不能依赖已切走的原生 USB 串口。
硬断电无法承诺数据不丢失；重启遇挂载失败应报告错误并停止写入。

## 存储管理与调试改动

- 现有 `MountCard()` 调用 `esp_vfs_fat_sdmmc_mount`。引入 MSC 后，需拆分低层
  SD 初始化和 FAT 挂载，统一由存储管理层持有 card/owner，避免重复注册 diskio。
- 第二阶段如做不重启切换，需要 NORMAL / QUIESCING / USB / RESTORING / ERROR
  状态、扫描取消及完成握手、解码器关闭握手、文件访问门禁和所有权串行化。
  `card_mutex` 当前只保护曲库等状态，不等于对所有 SD I/O 的排他控制。
- 固件与 Mac 不能同时挂载同一 FAT 卷；即使固件仅仅读取，也可能读到电脑更新
  前的缓存。传输模式全部暂停卡上播放、扫描、缓存更新。
- 使用 `do_not_format=true` 和 `format_if_mount_failed=false`（以锁定组件版本
  的实际结构为准）；不调用格式化 API。删去示例创建 README 等演示写入。
- 新版官方文档指出 set_storage_mount_point 返回值不传播所有内部挂载错误；
  需要等待 MOUNT_COMPLETE / MOUNT_FAILED / FORMAT_REQUIRED 事件并校验 owner，
  不能只根据 ESP_OK 显示“已完成”。确认锁定版本行为，并禁用不符合显式模式
  的自动交接策略（如该版本提供 auto_mount_off）。回调只发事件，不做耗时扫描。
- 第一版可保留正常模式原生串口，传输模式只启用 MSC；诊断优先验证 Hub 的
  CH340/UART0。若确需同一原生接口串口共存，则使用 TinyUSB CDC+MSC，迁移
  music-test RX/TX 与主机端口发现逻辑。CDC 不等于保留原生硬件 JTAG。
- 恢复烧录保留 BOOT+RESET 进入 ROM 下载模式的路径并实测；不烧 eFuse。
  端口重新枚举后需重新发现，不能把现有 /dev/cu.usbmodem 路径写死。

## 验收顺序

先完成编译和存储状态测试，再获用户当次授权烧录。首轮只读导出，读取目录和
现有缓存、导出曲库名单，核对 340 条缓存与真实 MP3 路径；不修改现有歌曲。
随后经用户授权创建专用测试目录，复制测试 MP3、读取并比较 SHA-256、中文改名、
删除测试文件、安全推出、返回助手、索引刷新、播放/语音恢复。异常拔线/断电
使用独立测试卡，不拿用户现有歌库做破坏性实验。

后续 MSC 源码已构建；尚未刷入、未在 Finder 枚举磁盘、未验证 CH340 驱动或实际速度。
本调研证明有官方代码和硬件设计依据，不代表功能已实现或已实机验收。
