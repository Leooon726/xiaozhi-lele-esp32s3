# 不经过云端的本地音乐调试

目的：唤醒/云端 MCP 已经可以调用工具时，直接测试 SD → MP3 → 重采样 → PCM 队列 → 扬声器，避免每次让用户重复说话。

## 开关与安全边界

需要 `lichuang-dev-lele-music-debug`，它额外启用 `CONFIG_LICHUANG_TF_MUSIC_DEBUG`。普通变体默认关闭。仅本机 USB 输入和板上按键，无网络监听、无 shell、无任意文件读取接口。

串口协议为 ASCII 行：

```text
music-test status
music-test play 0
music-test stop
music-test verify 1
```

扩容版已刷入，最近实机曲库为 340 首。编号从 0 开始，命令语法允许 0..1023，实际范围取决于已加载曲库。曲库按路径排序，重建后编号可能变化。串口播放只接受 idle；状态 busy 时先停止或等待。每行最多 64 字节，溢出整行丢弃。只有已索引 MP3 能被选择。

它和 MCP `tf_play` 最终都走 `Application::RequestMusic`；USB 不模拟云端语音会话，不能据此证明所有云端状态时序正常。

## Mac 侧控制

在已装 pyserial 的环境运行，由 agent 执行即可：

```bash
python scripts/lele/music_debug.py status
python scripts/lele/music_debug.py play 0 --seconds 20
python scripts/lele/music_debug.py stop
# 多设备时添加 --port /dev/cu.usbmodem...
```

打开原生 USB 可能复位设备。脚本等待启动和曲库就绪，限速发送状态探测，写入有超时；自动控制不需要 BOOT/RESET。如果连接异常，不要同时开另一个监视器抢串口。

`--seconds` 是采集时长，不是歌曲时长；脚本退出不会自动停歌。新进程重新开端口又可能复位，连续交互最好在同一串口连接内进行。

原始日志在被忽略的 `.local/logs/`，可能含联网或对话信息，不得公开上传。终端筛选输出也需审阅后才能贴到公开 issue。

## 物理按键

旧版长按 **BOOT** 接入首曲播放/停止；当前已刷入的两个音乐变体启用 USB 菜单，长按约 2 秒改为打开/关闭菜单；USB 点播命令保持。关闭 CONFIG_LICHUANG_USB_STORAGE 的 debug 变体保留旧长按动作。不要在 RESET/上电时按住 BOOT（那会进入下载模式）。菜单代码已刷入，物理交互待验收，详见 [USB_TRANSFER.md](USB_TRANSFER.md)。

## 日志判读

| 日志/阶段 | 判断 |
|---|---|
| `TfMusicDebug: USB ready` | 本地输入任务初始化成功；还需 status 回复证明收命令 |
| `ready=1 tracks=... playback=... state=...` | 曲库和播放状态 |
| `Play index=... path=...` | 实际选中的文件，用于不同入口对照 |
| `TF MP3 file opened` | FAT 文件打开通过，尚不代表解码 |
| `Skipping ID3v2` / `MP3 decoder opened` | 标签解析、解码器初始化 |
| `PCM format: rate=... bits=... channels=...` | 已有 PCM 帧，接下来可能重采样 |
| `Failed: stage=... error=...` | 具体失败位置及 SDK 数字错误码；同时输出内存统计 |
| `stage=buffers` / `decoder_open` | 首先排查分配及解码器初始化 |
| `decode_frame` / `pcm_format` | 文件编码/损坏/格式或接口使用；结合 consumed/needed/输出大小 |
| `resampler_open/capacity/process` | 重采样参数、内存和 SDK 返回码 |
| `pcm_queue` | 队列或取消条件，结合 playback_id 和状态事件 |
| `decoder_no_progress` | 缓冲/EOF 未前进，检查输入与错误策略 |
| `stack free=...` | 任务栈余量；不要恢复 32 KiB 内部 SRAM 栈 |

music playback 数字：0 idle、1 queued、2 playing、3 completed、4 failed、5 stopped。`Notification playback ... failed` 为汇总，不能单独断言故障原因。

先保留选曲和阶段证据，再改代码；不同 MP3 的测试不可混为同一次。坏 MP3 应可失败回待机，不应造成重启。若分配失败，区分 free 总量和最大连续块。

## 云端点歌流程

提示模型先 `self.music.tf_search`，query 可为空；从结果复制原样 path 调用 `self.music.tf_play`。正常点歌不要每次 refresh；用户明确要求重建时才 `tf_refresh`。停止用 `self.music.stop`，结果需以 `self.music.status` 和实际声音确认。

原模型角色提示词未自动修改账号；如服务端行为不同，先核对实际调用日志。语音点歌会把文件名/匹配路径发给当前小智服务；本地 USB 点播本身不需要云端，但设备仍可正常联网。

传输模式中原生 USB 接口是 MSC 磁盘，不运行 music-test 接收任务；不要运行主机点播脚本或假定端口仍为 303A:1001。退出回助手后重新发现串口。

## SD 本机重复读取校验（新源码，尚未刷入）

`music-test verify <编号>` 仅 debug 版的本机 USB 可调用，不能指定任意路径。
等待 idle、播放完全空闲且 scanning=0 后由主任务认领存储，独立低优先级 worker
重新打开同一首 MP3 三次，只读计算 SHA-256、字节数、耗时及 matches_first/all_equal。
不写歌曲、索引或配置，不改 SD 时钟，也不改变解码容错策略。
每遍最多 16 MiB，整个检查最多 90 秒（检查点限制，单次底层 I/O 仍受 SDK 超时约束）；
4 KiB 输入缓冲及 6 KiB 任务栈放 PSRAM，匹配 WithCaps 删除。

```bash
python scripts/lele/music_debug.py verify 1 --seconds 120
```

校验过程中阻止点歌和新扫描；传输请求会取消校验，USB/网页必须等文件关闭后
才能接管 SD。进入对话、超时或 `music-test stop` 也取消。所有退出路径在关闭文件、
释放缓冲后才归还门禁；任务创建失败恢复，转入传输则保留传输持有的门禁。
只输出摘要和编号，不回传文件正文；摘要/日志留本机，不能公开上传歌单或原始日志。
工具未观察到 Done 时报告不完整，不能将发出命令当作校验完成。

三遍一致只说明该观察窗口的板内读取一致，不证明原文件正确；三遍不一致则继续
查板内 SD/内存路径。再与 USB 副本摘要对照，隔离 MSC/Mac 路径。
对应现有失败和 USB 读取证据见 [SD_PLAYBACK.md](SD_PLAYBACK.md)。
