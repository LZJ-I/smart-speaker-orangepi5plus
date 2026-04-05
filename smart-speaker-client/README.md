# smart-speaker-client

![客户端模块（霓虹赛博）](../readme-illustrations/neon-client-focus.svg)

智能音箱客户端，当前拆成 `player`、`voice-assistant`、`supervisor`、`ipc`、`tools`、`assets`、`docs`；运行期配置与日志在 **`data/`**（已 `.gitignore`，见 `player/core/runtime_config.c`）。

## 目录

### 仓库结构

- `player/`：播放控制、规则匹配、音乐源后端（实现分布在 `core/`、`net/`、`select_loop/`、`device/`、`rules/`、`bridge/`、`music_source/`）
- `player/music_source/`：本地曲库后端、server 曲库后端、统一路由
- `voice-assistant/`：ASR/KWS/TTS/LLM
- `supervisor/`：拉起与守护子进程
- `ipc/`：二进制 IPC
- `tools/`：调试工具
- `assets/`：静态资源（如 TTS 等）
- `docs/`：技术说明与索引

### 本文档章节索引

1. 生产环境依赖（请先安装）
2. 本地最小可运行
3. 完整构建
4. 运行
5. 运行时配置（player，`data/config/client.toml`）
6. 可选环境变量（调试与其它）
7. 环境变量（ASR / KWS 录音设备）
8. 音乐源
9. Server 联调
10. 模型下载
11. 文档

## 生产环境依赖（请先安装）

`player` 正常编译与播放依赖下列开发包；**若缺少头文件或链接失败，请先安装，不要依赖静默降级路径**（降级实现计划移除）。

**Debian / Ubuntu / Armbian：**

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  pkg-config \
  libjson-c-dev \
  libasound2-dev \
  libasound2-plugins \
  libgstreamer1.0-dev \
  libgstreamer-plugins-base1.0-dev \
  gstreamer1.0-tools \
  gstreamer1.0-alsa \
  gstreamer1.0-plugins-base \
  gstreamer1.0-plugins-good \
  gstreamer1.0-plugins-ugly \
  gstreamer1.0-libav
```

- **MP3 等**：常依赖 `gstreamer1.0-plugins-good` / `gstreamer1.0-plugins-ugly`（上表已一并安装）。
- **AAC（在线 URL 常见 `.aac` / MPEG-4 AAC）**：依赖 **`gstreamer1.0-libav`**（`avdec_aac`）。若不想拉 FFmpeg 相关依赖，可改用 `gstreamer1.0-plugins-bad`（含 faad），二选一即可；验证：`gst-inspect-1.0 avdec_aac` 或 `gst-inspect-1.0 faad`。

**GStreamer 运行期与 `playbin create fail`**：日志里 `(E) GST playbin create fail` 表示 `gst_element_factory_make("playbin")` 失败，**与歌曲 URL、在线/本地无关**，一般是系统里 **没有加载 `playbin` 插件**（`playbin` 在 **`gstreamer1.0-plugins-base`** 的 `libgstplayback.so` 中）。仅安装 `libgstreamer1.0-dev` 等开发包**不会**把插件装进运行环境，板子上需额外安装例如：

```bash
sudo apt install -y gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly gstreamer1.0-libav
```

验证方式二选一：`sudo apt install gstreamer1.0-tools` 后执行 `gst-inspect-1.0 playbin`（**未装该包时会出现 `command not found`，属正常**）；或直接看插件目录是否存在 `libgstplayback.so`，例如 `ls /usr/lib/aarch64-linux-gnu/gstreamer-1.0/`（架构不同则把 `aarch64-linux-gnu` 换成本机 `gcc -print-multiarch` 输出）。

**服务端曲库目录**：音频文件若直接放在曲库**根目录**（非 `歌手名/歌曲.mp3` 结构），`list_music` / `search_music` 返回里 **`singer` 可能为空**，属当前扫描逻辑预期。

**扬声器 / ALSA（板载 ES8388）**：`aplay -l` 里 **card 3 `rockchipes8388`** 对应 3.5mm / 板载模拟输出；当前列表里**没有** USB 声卡时，USB 音箱不会被系统识别。`amixer -c 3` 可看 `Headphone`/`Speaker`/`PCM` 是否静音。若 `~/.asoundrc` 里配置了 `pcm.!default` → `dmix`+`hw:3,0`，可用环境变量 **`SMART_SPEAKER_GST_ALSA_DEVICE=default`** 让 `alsasink` 与系统默认一致；默认仍为 `player/core/player_constants.h` 里的 `dmix:CARD=rockchipes8388,DEV=0`。**其它进程独占 `hw:3,0`**（例如 `gst-launch` 直连硬件）时，`aplay -D default` 可能出现 `unable to open slave` / `Device or resource busy`，需先结束占用进程再测声。

语音链路（ASR/KWS/TTS）另需本仓库 `3rdparty` 与模型，见下文「模型下载」。

## 本地最小可运行

```bash
cd smart-speaker-client
make -C player
# 挂载SD卡到本地目录，例如
# sudo mount /dev/mmcblk1p1 /mnt/sdcard
cd player
./run
# 首次运行后编辑（路径取决于 CWD，见下节「运行时配置」）
# nano ../data/config/client.toml   # 若在 player/ 下启动
# 将 local_music_root 改为你的曲库根目录，或保持默认 /mnt/sdcard/
```

本地曲库目录建议：

```text
/path/to/music/
├── 稻香.mp3
├── 晴天.mp3
└── 演员.flac

```

未改 `client.toml` 时，`local_music_root` 默认与 `player_constants.h` 中 `SDCARD_MOUNT_PATH` 一致（当前为 `/mnt/sdcard/`）。

## 完整构建

```bash
cd smart-speaker-client
make
```

完整构建依赖：与上节 **生产环境依赖** 一致，外加 `sherpa-onnx` 与各子模块所需库（见各目录说明）。

## 运行

- 本地运行 `player`：`cd player && ./run`
- 全链路运行：`make run`
- 停止：`make stop`

## 运行时配置（player，`data/config/client.toml`）

`player/core/runtime_config.c`：**首次启动**若配置文件不存在，会创建 `data/` 与 `data/config/client.toml`。键值对与默认说明写在文件头注释中；合法 `player_mode` 为 `auto`、`online`、`offline`；`music_search_source` 为 `tx` / `wy` / `kw` / `kg` / `mg` / `auto` / `all`。

**路径**：进程当前目录下若已有 `./fifo/asr_fifo`，则配置为 **`./data/config/client.toml`**（通常为客户端仓库根经 `init.sh` 后的工作目录）；若在 `player/` 下直接执行 `./run`，则为 **`../data/config/client.toml`**。

| 键 | 作用 |
|----|------|
| `server_ip` | TCP 长连与 `music_source_server` 对端 |
| `server_port` | 同上 |
| `local_music_root` | 本地曲库根目录 |
| `startup_volume` | 启动音量 0～100 |
| `player_mode` | `offline` 不连服务端；`auto`/`online` 先尝试 TCP |
| `gst_alsa_device` | 传给 GStreamer `alsasink` 的 device |
| `music_search_source` | 与 server 侧在线搜源语义对齐的默认源 |

`player/core/player_constants.h` 里仍有 `SMART_SPEAKER_*` 字符串宏名，**当前 player 运行时配置不读取这些环境变量**，仅以本 TOML（及首次写入时用的头文件宏默认值）为准。改默认可改头文件宏后删除旧 `client.toml` 再跑，或直接编辑 TOML。

## 可选环境变量（调试与其它）

| 变量 | 作用 | 代码位置 |
|------|------|----------|
| `SMART_SPEAKER_LINK_DEBUG` | 链路调试开关 | `player/net/link.c` |
| `SMART_SPEAKER_LINK_DEBUG_PATH` | 调试输出路径 | `player/net/link.c` |
| `GST_PLUGIN_PATH` | GStreamer 插件搜索路径；若存在仓库内 bundled 插件目录，启动逻辑会前置系统路径 | `player/core/player_gst.c` |

## 环境变量（ASR / KWS 录音设备）

`asr_kws_process`（及共用 `voice-assistant/common/alsa.c` 的录音初始化）通过 `arecord -l` 输出，按**名称子串**匹配 capture 设备。

| 变量 | 作用 | 默认值 |
|------|------|--------|
| `SMART_SPEAKER_ALSA_CAPTURE_KEYWORD` | 与 `arecord -l` 里某行 card 描述中一致的子串（如板载 mic 常为 `rockchipes8388`，USB 设备名则因机而异） | `MICCM379`（与 `voice-assistant/common/alsa.h` 中 `RECORD_DEVICE_DEFAULT` 一致） |

未设置或为空字符串时回退默认。示例：`export SMART_SPEAKER_ALSA_CAPTURE_KEYWORD=rockchipes8388` 后在同一终端运行 `./asr_kws_process`。

## 音乐源

- 本地：`file://...`，根目录为 `client.toml` 的 `local_music_root`
- server：TCP 对端为 `client.toml` 的 `server_ip`:`server_port`；可播 URL 主要来自服务端 JSON 的 `play_url`（在线解析链路由 server 配置，见 `smart-speaker-server/README.md`）
- 在线/离线：`client.toml` 的 `player_mode`

## Server 联调

在线解析与本地曲库扫描依赖 **`smart-speaker-server`** 及其 **`data/config/*.toml`**（`music_root` 等），详见服务端 README。

- TCP：默认 `127.0.0.1:8888`；连其它主机时在 **`client.toml`** 改 `server_ip` / `server_port`
- 协议：`list_music`、`search_music` 等见 `smart-speaker-server/docs/接口定义.txt`

**目录说明**：`make`、`make tests`、`./tests/test_client` 必须在 **`smart-speaker-server`** 目录执行，不是在 `smart-speaker-client` 下。与 client 同级时：

```bash
cd ../smart-speaker-server
make
make tests
# 测 TCP 连对端：环境变量 SMART_SPEAKER_SERVER_IP / PORT（见 tests/test_client.c）
./tests/test_client
```

仓库根在别处的，用绝对路径，例如：`cd /home/orangepi/smart-speaker-orangepi5plus/smart-speaker-server`。

### 在线搜歌 → HTTP URL 小测（不跑完整 `run`）

需 **`./server_smart_speaker` 已启动**（及 `music.toml` / Node 子服务就绪）。在 `player` 目录：

```bash
make test_online_music_chain
# 对端与 player 一致：先配好 data/config/client.toml 的 server_ip / server_port（在含 fifo 的工作目录或 ../data）
./test_online_music_chain 雪
```

会打印 `url=` 等；可用 `curl -I` 抽查 `play_url` 是否可访问。加 `--play` 会尝试 `gst-launch-1.0 playbin` 试播（需 GStreamer；失败可忽略）。

### 完整 `run` 调试（长连 + 搜歌 + 播放）

1. **先起**：`mysqld`、`./server_smart_speaker`（见服务端 README：`music.toml`、Node `music-service`）；服务端本地扫描目录为 **`data/config/server.toml`** 的 `music_root`（默认 `data/music-library/`）。
2. **客户端**：在 `client.toml` 中设置 `server_ip` / `server_port`（及按需 `player_mode`）；与默认不一致时必改，本机联调可保持默认。
3. **`player/core/main.c` 已重新启用 `socket_init()`**：成功则日志有 `TCP 长连成功`；失败则 `TCP 长连失败`（**不会**自动走 `player_switch_offline_mode`，避免无 U 盘时被 TTS/挂载逻辑卡死）。失败时搜歌仍可用 `music_source_server` 的短时连接 + 本地回退。在线列表/解析由 **服务端** `list_music` 等与 **music-service**（及 Rust `music-lib`）协同返回 `play_url`；泛说「听歌/热门」等仍走服务端本地曲库分页。
4. **看日志**：`player/core/main.c` 顶部 `LOG_LEVEL`（默认 4）控制 `debug_log.h` 输出；终端跑 `./run` 直接看。关键 TAG：`PLAYER-MAIN`、`SOCKET`、`PLAYER`。可 `grep SOCKET` 或全文检索 `连接服务器`。
5. **分层排障**：先 `./test_online_music_chain`；再 `curl -I` 打印出的 `url`；最后 `./run`。服务端终端应出现新 TCP 连接；若只有搜歌无长连，检查是否 `socket_init` 报错。
6. **GDB**（可选）：`gdb --args ./run`，断点示例 `b socket_init`、`b music_source_server_search`。

## 模型下载

链接与包名与分支 `legacy/music-lib` 的 README 一致；**解压位置按本仓库约定**（相对路径均以 `smart-speaker-client/` 为根）。

以下命令在 **`smart-speaker-client` 目录下** 逐段执行即可。

**关于 JNI 包 vs shared-cpu（v1.12.25 linux-aarch64）：** JNI 包含 `include/` 与 `lib/`，但 `lib/` 内是 `libsherpa-onnx-jni.so` 等，**没有** `libsherpa-onnx-c-api.so`。shared-cpu 包含 `libsherpa-onnx-c-api.so`、`libonnxruntime.so`（及 `bin/`），**该 tarball 不含 `include/`**。本仓库用 C API：**`-I` 用 JNI 目录，`-L` 用 shared-cpu 目录**，两套都解压。

### 1. sherpa-onnx 预编译（均在 `3rdparty/sherpa-onnx/`）

#### 1a JNI（头文件，`Makefile` 的 `-I`）

解压后必须出现 **`3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-jni/`**（含 `include/`）。

```bash
mkdir -p 3rdparty/sherpa-onnx
cd 3rdparty/sherpa-onnx
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.25/sherpa-onnx-v1.12.25-linux-aarch64-jni.tar.bz2
tar -jxf sherpa-onnx-v1.12.25-linux-aarch64-jni.tar.bz2
cd ../..
```

#### 1b 共享库 CPU（链接 `libsherpa-onnx-c-api`，`Makefile` 的 `-L`）

解压后必须出现 **`3rdparty/sherpa-onnx/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu/`**（含 `lib/`）。与 JNI 同目录并存即可。

```bash
mkdir -p 3rdparty/sherpa-onnx
cd 3rdparty/sherpa-onnx
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/v1.12.25/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu.tar.bz2
tar -jxf sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu.tar.bz2
cd ../..
```

### 2. ASR（SenseVoice）

解压后必须出现目录 **`3rdparty/model/asr/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/`**。

```bash
mkdir -p 3rdparty/model/asr
cd 3rdparty/model/asr
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
tar -jxf sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17.tar.bz2
cd ../../..
```

### 3. VAD（Silero）

文件必须落在 **`3rdparty/model/asr/silero_vad.onnx`**（与 ASR 模型目录同级）。

```bash
mkdir -p 3rdparty/model/asr
wget -O 3rdparty/model/asr/silero_vad.onnx \
  https://github.com/k2-fsa/sherpa-onnx/releases/download/asr-models/silero_vad.onnx
```

### 4. KWS（Zipformer）

解压后必须出现目录 **`3rdparty/model/kws/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/`**。

```bash
mkdir -p 3rdparty/model/kws
cd 3rdparty/model/kws
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/kws-models/sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2
tar -jxf sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20.tar.bz2
cd ../../..
```

### 5. TTS（Matcha）

解压后必须出现目录 **`3rdparty/model/tts/matcha-icefall-zh-en/`**。

```bash
mkdir -p 3rdparty/model/tts
cd 3rdparty/model/tts
wget https://github.com/k2-fsa/sherpa-onnx/releases/download/tts-models/matcha-icefall-zh-en.tar.bz2
tar -jxf matcha-icefall-zh-en.tar.bz2
cd ../../..
```

### 6. Vocoder（须在 TTS 目录内）

文件必须落在 **`3rdparty/model/tts/matcha-icefall-zh-en/vocos-16khz-univ.onnx`**（先完成上一节解压）。

```bash
wget -O 3rdparty/model/tts/matcha-icefall-zh-en/vocos-16khz-univ.onnx \
  https://hk.gh-proxy.org/https://github.com/k2-fsa/sherpa-onnx/releases/download/vocoder-models/vocos-16khz-univ.onnx
```

直连 GitHub 时可把 URL 换成去掉 `https://hk.gh-proxy.org/` 前缀的原始地址。

### 解压完成后的目录检查

```text
smart-speaker-client/
└── 3rdparty/
    ├── sherpa-onnx/
    │   ├── sherpa-onnx-v1.12.25-linux-aarch64-jni/
    │   │   ├── include/
    │   │   └── lib/
    │   └── sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu/
    │       ├── bin/
    │       └── lib/
    └── model/
        ├── asr/
        │   ├── silero_vad.onnx
        │   └── sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/
        ├── kws/
        │   └── sherpa-onnx-kws-zipformer-zh-en-3M-2025-12-20/
        └── tts/
            └── matcha-icefall-zh-en/
                └── vocos-16khz-univ.onnx   # 以及 matcha 包内其余文件
```

## 文档

- [docs/smart-speaker-client-技术文档索引.md](docs/smart-speaker-client-技术文档索引.md)
