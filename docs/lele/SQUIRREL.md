# 松鼠表情（F-011）

用户已认可待机母版。本轮使用内置 imagegen，以同一母版分别生成六种表情，
保持配色、身体和尾巴位置；不是网上抓取的角色图。源码 d0b44f2d3f6ce065c8cc5fde36bac5ab5ecabd82 已提交并发布
[test-d0b44f2d3f6c-01](testing/test-d0b44f2d3f6c-01.md) Pre-release，Mac 已刷入；2026-11-21 用户确认基本显示/对话正常，详见版本记录。

## 状态与表情

| 资源 | 使用场景 |
|---|---|
| squirrel_neutral.png | 启动、普通待机、中性表情 |
| squirrel_listening.png | 设备进入聆听；随后收到 neutral 仍保持聆听形象 |
| squirrel_speaking.png | 设备进入说话/通知；云端具体情绪可继续切换 |
| squirrel_thinking.png | 连接等待、thinking、surprised、shocked |
| squirrel_happy.png | happy、laughing、funny、loving、winking、kissy、cool、delicious、confident、embarrassed |
| squirrel_sad.png | sad、crying、angry，使用温和难过表情 |
| squirrel_sleepy.png | sleepy、relaxed，闭眼休息表情 |

仅本板两个音乐变体的默认 LVGL UI 启用；其他板、stock、WeChat 和 Emote 不替换。
本轮是七张静态图按状态切换，没有持续播放的 GIF、眨眼、口型同步或音量驱动。
未知情绪返回不支持并保留当前画面，不切回旧 emoji 图案。

## 素材与实现

- 固件图片：main/boards/lckfb/szpi-esp32s3/assets/squirrel/，7 张 144×144 RGBA PNG，
  共 144,630 bytes（约 141 KiB）。使用共同裁剪范围、等比例缩小及透明补边，
  不对各表情单独放大，减少状态切换时的尺寸/位置跳动。
- 原始编辑提示词保存在同目录 PROMPTS.md，母版提示词在 MASTER_PROMPT.md。
  原始大图保存在开发机忽略目录 .local/artwork/squirrel/source/，不被固件引用；
  仓库内 PNG 即可跨机器复现编译，不需要重新生成图片或安装图像模型。
- main/CMakeLists.txt 仅在启用该 UI 时把 PNG 嵌入应用 Flash，不依赖 SD 或下载。
  squirrel_images.* 提供生命周期稳定的 LVGL 描述符；不改变原资源分区内容。
- lele_display.h 在原 SpiLcdDisplay 上接入表情/状态替换；沿用原布局、主题、
  预览图、字幕、横竖屏和触摸区域，不创建新的音频或网络流程。
  同一表情重复请求不会重复设置图片源。
- PNG 由现有 LodePNG 解码器处理；每张 144×144 RGBA 像素约 81 KiB，此外
  还有 PNG 解码临时内存及 LVGL 开销。目标已配置 1 MiB LVGL 内存池，图片缓存
  为零；不能把文件压缩大小当作运行内存，也不预先把七张原图展开常驻。

## 验证与待测

- 生产状态/情绪方法主机回归：启动、中性、聆听、连接、说话、云端情绪映射，
  空/未知输入、显示未初始化、锁失败、停止旧 GIF、重复图片源等分支。
- 真实 LVGL 解码七张实际 PNG，检查尺寸与解码成功；沿用两方向的布局/触摸回归。
  主机测试采用与目标相同的 1 MiB LVGL 池，避免默认 64 KiB 池不足而误报。
- 完整主机套件 Ran 227 tests、OK (skipped=5)，无失败；跳过原有 cJSON/mbedTLS
  两组、numpy/Pillow/packaging 两项、OpenSSL 开发库一项，不计为通过。
- ESP-IDF v6.1.1 debug 固件构建/链接/合并通过，应用 3,149,840 bytes，槽余
  24%。应用内七张 PNG 与源文件逐字节一致；五分区/NVS 范围及合并 ZIP 内容
  核对通过。canonical ZIP 仍不是可交付的五文件安全烧录包。
- 普通音乐、stock 无音乐、WeChat、Emote 下五个翻译单元共 20 项条件语法
  检查通过，不代表四个完整固件链接。结果同步到 TESTING.md 的 F-011。

实机尚未执行：

| ID | 操作 | 预期 |
|---|---|---|
| Q-01 | 启动后待机，分别横屏/竖屏、深色/浅色 | 松鼠透明边缘正确，位置合理，无旧机器人/emoji 跳回 |
| Q-02 | 唤醒、聆听、等待连接、播报、结束对话 | 表情随状态切换，字幕/预览图不失效，无卡顿或重启 |
| Q-03 | 在可触发的对话中检查开心/难过/思考/困倦等情绪 | 同一角色，图像切换位置稳定；无法触发的项保持未测 |
| Q-04 | 音乐播放、Songs、方向切换、Wi-Fi/USB 传输返回，连续操作 | 原有功能和触摸正常，返回后恢复松鼠；记录内存与音频表现 |

困倦表情只是一张图，不等于息屏。先前讨论的“空闲 60 秒关闭显示、语音/触摸
亮屏且首触仅唤醒”是下一项独立开发，当前还没有接入，不能宣称已实现。

## 后续尺寸调整（2026-11-21，已上板待视觉确认）

用户确认原尺寸基本显示和交互正常后，要求放大至原来 1.8 倍。仅本板松鼠
渲染使用 LVGL scale 461/256（约 1.8008 倍），保持原中心、原七张 PNG，
开启透明表情容器的 overflow-visible，防止按 144px 原容器裁剪。素材、字幕、
其他板型及相机预览缩放均不改。144px 画布变换后约 259px，超过屏幕短边
240px；不能宣称所有可见边缘和字幕间距均正常，实际构图待上屏验收。
新策略 54b53009b4ad 已按当次授权完整备份/校验后刷入，启动及 346 首曲库正常；
放大后视觉与交互尚待用户确认，见 [版本记录](testing/test-54b53009b4ad-01.md)。
