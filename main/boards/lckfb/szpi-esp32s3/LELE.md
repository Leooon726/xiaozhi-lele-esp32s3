# Lele wake-word test variant

Based on XiaoZhi v2.6.0 and the unchanged LiChuang ESP32-S3 board implementation.
The `lichuang-dev-lele` build keeps the stock hardware pins and 16 MiB partition layout.
The reported board name distinguishes this local test build from the official firmware.

Build with an activated ESP-IDF 6.1 environment:

```sh
python scripts/build.py lckfb/szpi-esp32s3 --name lichuang-dev-lele --language en-US --wake-word 'custom:le le:乐乐' --zip
```

The Chinese MultiNet 7 model recognizes `le le`, displayed as `乐乐`.
MultiNet 6 exhausted runtime memory alongside this board's display and AFE;
MultiNet 7 uses less PSRAM and internal RAM according to the ESP-SR benchmark.
Threshold starts at the upstream default 20. This two-syllable phrase needs physical
false-trigger and missed-trigger testing. Interface language stays English for comparison
with the official release used in the baseline test.

Validate the startup model and active command in serial logs, then test repeated wake-ups,
silence/background speech, button conversation, and normal voice replies. Build success
alone does not establish recognition quality.
