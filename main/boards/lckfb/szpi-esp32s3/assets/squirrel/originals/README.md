# 松鼠高清原图

七张原始 imagegen 输出，均为 1448×1086 RGBA PNG，合计 7,148,298 bytes。
neutral 是用户认可的待机母版，其他六张以同一母版编辑；提示词见上级目录
MASTER_PROMPT.md 和 PROMPTS.md。此处保留原始文件字节，不用 144px 运行图反向放大。

这些文件只作设计源素材，main/CMakeLists.txt 的固件嵌入清单不包含 originals/。
固件使用上级目录 squirrel_*.png：直接从这里的原图等比例缩小到 259×259，
配合 LVGL 1:1 显示，维持之前约 1.8 倍的构图。

在仓库根目录，用独立 Python 环境安装脚本依赖，然后生成或只核对素材：

~~~bash
python -m pip install -r scripts/lele/requirements-artwork.txt
python scripts/lele/prepare_squirrel.py
python scripts/lele/prepare_squirrel.py --check
~~~

使用固定 Pillow 12.3.0。脚本对七张原图使用相同裁剪框和透明补边，保持身体、
尾巴的位置关系，不给每张表情单独裁剪/放大。259px 资源不经中间的 144px 图片。
运行此脚本不会启动 ESP-IDF 编译，也不会操作设备。
