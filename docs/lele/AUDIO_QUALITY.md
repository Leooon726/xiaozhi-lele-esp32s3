# AI 音频流畅度（F-013）

2026-11-21 当前实机 1b001b5，云端回复断续已复现，用户称从第一秒开始。
SD 歌曲提前退出被用户明确暂缓，不在本轮修复范围。原始音频/串口/凭据不入库。

## 观察与边界

- [原版实机记录](testing/test-1b001b5af08e-01.md) 轮次 3：MQTT/UDP 音频
  83 次序号跳跃、96 个未见序号，没有迟到/重复警告。该证据只能确定接收序列
  不完整，尚不能确定服务器、公网、AP、Wi-Fi 驱动或 UDP socket 的责任层。
- 局域网 ICMP 20 包无丢失不代表云端 UDP 正常；无逐包网络抓包或端到端序号对照。
- 旧配置 Wi-Fi static RX=3、dynamic RX=6、BA=3，UDP mailbox=6、TCPIP=16。
  云端音频交给 AudioService 后，解码队列满时默认拒收，调用方原本没有拒收统计。
- 内部 SRAM 历史最低约 13 KiB，不能无约束扩张内部缓冲；当前最大空闲不是峰值保证。

## 第一版候选（尚未上板）

仅两个本板音乐变体配置改为 static RX=6、dynamic RX=16、BA=6，
UDP mailbox=32、TCPIP mailbox=64。启用 SDK 的 SPIRAM_TRY_ALLOCATE_WIFI_LWIP，
可迁移的网络内存优先 PSRAM，DMA 必需内存仍由 SDK 分配；不改系统/其他板型。
这是基于接收突发拥塞假设的对照候选，不宣称已经解决现有缺包。

DEBUG 音乐版每 5 秒在 speaking/listening 记录一次 AudioDiag：

- decode_seen：传入解码队列 API 的累计调用，不含应用状态门禁之前被忽略的数据。
- queue_drop：非阻塞调用遇到解码队列满的累计次数。
- generation_drop：服务停止或播放 generation 变化造成取消，不与队列满混算。
- decode_q、pcm_q、max_decode_q、decoding、outputting：瞬时队列/执行状态和高水位。

统计受原队列锁保护，日志在锁外且由主任务低频输出，不在音频实时任务逐包打印。
不改 PCM/Opus 队列容量、接收任务优先级、解码算法、音量、播放/打断行为。
累计计数包括通知/启动音等经过同一 API 的数据，不能当作云端原始 UDP 接收总数。

SDK primary references：
[Wi-Fi performance](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/wifi-driver/wifi-performance-and-power-save.html)，
[lwIP](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/lwip.html)。
当前 SDK Kconfig 说明 UDP 邮箱满会丢包，增大邮箱/接收缓冲有内存成本；
此处采用有界值，必须以实机内存及前后缺口/queue_drop/听感对照验证。

## 验证和下一步

新增主机回归执行真实 PushPacketToDecodeQueue / LogPlaybackDiagnostics 方法，
核对满队列拒收、等待调用被 generation 变更取消、服务停止计数，以及日志不持队列锁。
完整 debug 构建/打包通过；主机 233 项（229 通过、4 跳过），三个条件配置共 21 个
翻译单元编译通过（不是三个完整链接）。真实 sdkconfig 和五文件校验已通过，
固定源码及包摘要见 [候选记录](testing/test-b91b6551de59-01.md)；仍未刷入。
烧录需用户当次确认、16 MiB 完整备份/设备端校验；刷入后让同一句长回复做对照，
统计序号缺口、本地 queue_drop、队列水位与听感。若缺口未改善，需要继续区分
接收任务调度、网络路径/服务端及其他拥塞，不将增加缓冲视作结案。
