# smart-speaker-client

![客户端模块（霓虹赛博）](../readme-illustrations/neon-client-focus.svg)

智能音箱客户端，当前拆成 `player`、`voice-assistant`、`supervisor`、`ipc`、`tools`、`assets`、`docs`。

## 目录

- `player/`：播放控制、规则匹配、音乐源后端
- `player/music_source/`：本地曲库后端、server 曲库后端、统一路由
- `voice-assistant/`：ASR/KWS/TTS/LLM
- `supervisor/`：拉起与守护子进程
- `ipc/`：二进制 IPC
- `tools/`：调试工具

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
  gstreamer1.0-plugins-good
```

MP3 等解码仍可能需要：`gstreamer1.0-plugins-ugly`。

**GStreamer 运行期与 `playbin create fail`**：日志里 `(E) GST playbin create fail` 表示 `gst_element_factory_make("playbin")` 失败，**与歌曲 URL、在线/本地无关**，一般是系统里 **没有加载 `playbin` 插件**（`playbin` 在 **`gstreamer1.0-plugins-base`** 的 `libgstplayback.so` 中）。仅安装 `libgstreamer1.0-dev` 等开发包**不会**把插件装进运行环境，板子上需额外安装例如：

```bash
sudo apt install -y gstreamer1.0-plugins-base gstreamer1.0-plugins-good gstreamer1.0-plugins-ugly
```

MP3 等解码常依赖 `good` / `ugly`。验证方式二选一：`sudo apt install gstreamer1.0-tools` 后执行 `gst-inspect-1.0 playbin`（**未装该包时会出现 `command not found`，属正常**）；或直接看插件目录是否存在 `libgstplayback.so`，例如 `ls /usr/lib/aarch64-linux-gnu/gstreamer-1.0/`（架构不同则把 `aarch64-linux-gnu` 换成本机 `gcc -print-multiarch` 输出）。

**服务端曲库目录**：音频文件若直接放在曲库**根目录**（非 `歌手名/歌曲.mp3` 结构），`list_music` / `search_music` 返回里 **`singer` 可能为空**，属当前扫描逻辑预期。

**扬声器 / ALSA（板载 ES8388）**：`aplay -l` 里 **card 3 `rockchipes8388`** 对应 3.5mm / 板载模拟输出；当前列表里**没有** USB 声卡时，USB 音箱不会被系统识别。`amixer -c 3` 可看 `Headphone`/`Speaker`/`PCM` 是否静音。若 `~/.asoundrc` 里配置了 `pcm.!default` → `dmix`+`hw:3,0`，可用环境变量 **`SMART_SPEAKER_GST_ALSA_DEVICE=default`** 让 `alsasink` 与系统默认一致；默认仍为 `player_constants.h` 里的 `dmix:CARD=rockchipes8388,DEV=0`。**其它进程独占 `hw:3,0`**（例如 `gst-launch` 直连硬件）时，`aplay -D default` 可能出现 `unable to open slave` / `Device or resource busy`，需先结束占用进程再测声。

语音链路（ASR/KWS/TTS）另需本仓库 `3rdparty` 与模型，见下文「模型下载」。

## 本地最小可运行

```bash
cd smart-speaker-client
make -C player
# 挂载SD卡到本地目录，例如
# sudo mount /dev/mmcblk1p1 /mnt/sdcard
export SMART_SPEAKER_LOCAL_MUSIC_DIR=/path/to/music
cd player
./run
```

本地曲库目录建议：

```text
/path/to/music/
├── 稻香.mp3
├── 晴天.mp3
└── 演员.flac

```

`SMART_SPEAKER_LOCAL_MUSIC_DIR` 未设置时，默认扫描 `/mnt/sdcard/`。

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

## 环境变量（player）

宏名定义见 `player/player_constants.h`。下列为 `player` 进程会读取的变量；未设置时行为见「默认值」列。

| 变量 | 作用 | 默认值 |
|------|------|--------|
| `SMART_SPEAKER_LOCAL_MUSIC_DIR` | 本地曲库根目录 | `/mnt/sdcard/` |
| `SMART_SPEAKER_SERVER_IP` | server TCP / 曲库请求的 IP | `player_constants.h` 中 `SERVER_IP`（当前为 `127.0.0.1`） |
| `SMART_SPEAKER_SERVER_PORT` | server TCP 端口 | `8888`；非法字符串则回退默认 |
| `SMART_SPEAKER_SERVER_MUSIC_BASE_URL` | 在线歌曲 HTTP URL 前缀（`+ path`） | `http://<SMART_SPEAKER_SERVER_IP 或默认 IP>/music/` |
| `SMART_SPEAKER_PLAYER_MODE` | 设为 `offline`（大小写不敏感）时强制离线：不建 TCP 长连、并通知 ASR/KWS 离线；其它或未设视为在线 | 在线 |
| `SMART_SPEAKER_GST_ALSA_DEVICE` | 传给 GStreamer `alsasink` 的 device 字符串 | `dmix:CARD=rockchipes8388,DEV=0` |

**GStreamer**：若存在仓库内 `3rdparty/gstreamer-alsa/.../gstreamer-1.0` 插件目录，启动时会把该目录**前置**写入 `GST_PLUGIN_PATH`（环境已有则用 `新路径:旧路径`）。也可自行预先 `export GST_PLUGIN_PATH=...`。

**示例**

```bash
# 仅本地曲库、无 server
export SMART_SPEAKER_PLAYER_MODE=offline
export SMART_SPEAKER_LOCAL_MUSIC_DIR=/home/orangepi/Music
cd player && ./run
```

```bash
# 指定 server 与曲库 HTTP 前缀
export SMART_SPEAKER_SERVER_IP=192.168.1.10
export SMART_SPEAKER_SERVER_PORT=8888
export SMART_SPEAKER_SERVER_MUSIC_BASE_URL=http://192.168.1.10/music/
cd player && ./run
```

```bash
# 与系统默认 ALSA（如 ~/.asoundrc 的 pcm.!default）一致
export SMART_SPEAKER_GST_ALSA_DEVICE=default
cd player && ./run
```

## 音乐源

- 本地：`file://...`，扫描目录见上表 `SMART_SPEAKER_LOCAL_MUSIC_DIR`
- server：`list_music` / `search_music` 经 `SMART_SPEAKER_SERVER_IP:SMART_SPEAKER_SERVER_PORT`；播放 URL 为 `SMART_SPEAKER_SERVER_MUSIC_BASE_URL + <path>`
- 在线/离线由 `SMART_SPEAKER_PLAYER_MODE` 控制，见上表

## Server 联调

在线搜歌依赖 `smart-speaker-server` 与 Apache 静态目录：

- TCP：默认 `10.102.178.47:8888`，可用 `SMART_SPEAKER_SERVER_IP`、`SMART_SPEAKER_SERVER_PORT` 覆盖
- HTTP：默认 `http://<SMART_SPEAKER_SERVER_IP>/music/`，也可直接设 `SMART_SPEAKER_SERVER_MUSIC_BASE_URL`
- 协议：`list_music`（在线列表）、`search_music`（关键词）

**目录说明**：`make`、`make tests`、`./tests/test_client` 必须在 **`smart-speaker-server`** 目录执行，不是在 `smart-speaker-client` 下。与 client 同级时：

```bash
cd ../smart-speaker-server
make
make tests
export SMART_SPEAKER_SERVER_IP=127.0.0.1
./tests/test_client
```

仓库根在别处的，用绝对路径，例如：`cd /home/orangepi/smart-speaker-orangepi5plus/smart-speaker-server`。

### 在线搜歌 → HTTP URL 小测（不跑完整 `run`）

板子上需同时：**TCP 服务已起**、**Apache 可访问 `/music/`**。在 `player` 目录：

```bash
make test_online_music_chain
export SMART_SPEAKER_SERVER_IP=127.0.0.1
export SMART_SPEAKER_SERVER_PORT=8888
export SMART_SPEAKER_SERVER_MUSIC_BASE_URL=http://127.0.0.1/music/
./test_online_music_chain 雪
```

会打印 `path=` 与 **已百分号编码** 的 `url=`；可用 `curl -I` 打该 `url` 验证 Apache。加 `--play` 会尝试 `gst-launch-1.0 playbin` 试播（需 GStreamer；失败可忽略）。

### 完整 `run` 调试（长连 + 搜歌 + 播放）

1. **三件套先起**：`mysqld`、`./server_smart_speaker`、`apache2`；曲库在 `/var/www/html/music/` 或设好 `SMART_SPEAKER_MUSIC_PATH`（服务端）。
2. **环境变量**（与 `player_constants.h` 一致，按需改 IP）：
   ```bash
   export SMART_SPEAKER_SERVER_IP=127.0.0.1
   export SMART_SPEAKER_SERVER_PORT=8888
   export SMART_SPEAKER_SERVER_MUSIC_BASE_URL=http://127.0.0.1/music/
   ```
3. **`player/main.c` 已重新启用 `socket_init()`**：成功则日志有 `TCP 长连成功`；失败则 `TCP 长连失败`（**不会**自动走 `player_switch_offline_mode`，避免无 U 盘时被 TTS/挂载逻辑卡死）。失败时搜歌仍可用 `music_source_server` 的短时连接 + 本地回退。
4. **看日志**：`player/main.c` 顶部 `LOG_LEVEL`（默认 4）控制 `debug_log.h` 输出；终端跑 `./run` 直接看。关键 TAG：`PLAYER-MAIN`、`SOCKET`、`PLAYER`。可 `grep SOCKET` 或全文检索 `连接服务器`。
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
