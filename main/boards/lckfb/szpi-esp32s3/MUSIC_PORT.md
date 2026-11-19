# 乐乐 SD 音乐适配

当前说明集中维护，避免旧阶段记录被误认为现状：

- [项目交接](../../../../docs/lele/HANDOFF.md)
- [硬件和代码地图](../../../../docs/lele/ARCHITECTURE.md)
- [构建和烧录](../../../../docs/lele/BUILD_FLASH.md)
- [USB/BOOT 调试和云端调用](../../../../docs/lele/DEBUGGING.md)
- [实测和待验证边界](../../../../docs/lele/VALIDATION.md)
- [开发历史](../../../../docs/lele/HISTORY.md)

2026-11-19：整卡 MP3 缓存加载 128 首，USB 直接选歌/播放/停止实测通过，用户确认听到音乐。详细边界以交接和验证文档为准。

参考 MIT 项目 [jianglixi-oss/xiaozhi-nas-tf-music](https://github.com/jianglixi-oss/xiaozhi-nas-tf-music)，提交 `3ede3d71362ade67387792334a6c17ace298220d`。仅移植 SD MP3 部分。许可原文见 [第三方许可](../../../../third_party/licenses/xiaozhi-nas-tf-music.txt)。
