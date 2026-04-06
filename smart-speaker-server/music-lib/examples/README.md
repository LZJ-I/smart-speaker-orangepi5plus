# C 语言示例程序

> 在 **`music-lib/examples`** 下 `make` 构建。主服务侧集成见仓库 **`smart-speaker-server`**；板端通过 TCP 使用服务端能力，不直接链本目录。

本目录为 **libmusic_downloader** 的 C 调用示例（搜索、下载、取链）。音质降级、多首重试等行为由库内实现。

## 目录结构

```
examples/
├── search/                # 搜索示例
│   └── search.c          # 搜索程序（仅返回歌曲信息）
├── download/              # 下载示例
│   └── download.c        # 下载程序
├── search_and_download/   # 搜索并下载示例
│   └── search_and_download.c
├── url/                   # 获取直链示例
│   └── url.c             # 获取直链程序（平台+ID+音质）
├── Makefile               # 构建配置
├── music.h                # C 头文件
└── README.md              # 本文件
```

## 前置准备

在编译示例前，请先确保已经编译了 Rust 库：

```bash
cd ..
cargo build --release
cd examples
```

## 编译

```bash
# 编译所有示例程序
make

# 编译调试版本
make debug
```

## 重要说明 ⚠️

- **下载功能**：支持所有平台：`kw`、`mg`、`kg`、`tx`、`wy`
- **搜索功能**：仅支持：`tx`、`wy`、`auto`

## 示例程序

### 1️⃣ 搜索示例 (search/search.c)

搜索音乐并显示结果列表（仅返回歌曲信息，不获取下载链接，速度快）。

**参数顺序**：`<平台> <关键词>`

**运行方式**：
```bash
make search
# 或
./music_search <平台> <关键词>
```

**示例**：
```bash
# 使用 QQ 音乐搜索 "周杰伦"
./music_search tx "周杰伦"

# 使用自动选择搜索 "林俊杰"
./music_search auto "林俊杰"
```

---

### 2️⃣ 下载示例 (download/download.c)

根据歌曲 ID 直接下载音乐文件。

**参数顺序**：`<平台> <歌曲ID> <音质> <输出目录>`

**运行方式**：
```bash
make download
# 或
./music_download <平台> <歌曲ID> <音质> <输出目录>
```

**示例**：
```bash
# 使用 QQ 音乐下载某首歌曲，FLAC 音质，保存到 downloads 目录
./music_download tx "003aAYrm3GE0Ac" flac downloads

# 使用自动选择（先 tx 后 wy）
./music_download auto "003aAYrm3GE0Ac" flac downloads
```

---

### 3️⃣ 搜索并下载 (search_and_download/search_and_download.c) ⭐

**智能下载！** 搜索音乐后，自动尝试最佳音质和多首歌曲，确保下载成功率。

**核心逻辑**：
1. 搜索音乐，获取结果列表
2. 从第 1 首歌开始，最多尝试 5 首
3. 对每首歌，从目标音质开始，按优先级降级尝试
4. 只要有一首歌的任何音质下载成功，就成功返回
5. 所有尝试都失败时才报错

**音质优先级（高→低）**：
`master` > `atmos_plus` > `atmos` > `hires` > `flac24bit` > `flac` > `320k` > `128k`

**参数顺序**：`<平台> <关键词> <音质> <输出目录>`

**运行方式**：
```bash
make search-and-download
# 或
./music_search_and_download <平台> <关键词> <音质> <输出目录>
```

**示例**：
```bash
# 使用 QQ 音乐搜索 "周杰伦"，下载 FLAC 音质到 downloads 目录
./music_search_and_download tx "周杰伦" flac downloads

# 使用自动选择
./music_search_and_download auto "周杰伦" flac downloads
```

**功能特点**：
- 🔍 自动搜索并下载第一首匹配的歌曲
- 🎵 文件自动命名为：`歌曲名-艺术家名.扩展名`
- 🛡️ 自动清理文件名中的非法字符
- 📊 支持下载进度显示
- 🔄 智能音质降级，最大化下载成功率
- 🎯 自动歌曲切换，最多尝试 5 首

---

### 4️⃣ 获取直链示例 (url/url.c) ⭐

根据平台、歌曲 ID 和音质获取单个下载链接，无需下载文件。

**参数顺序**：`<平台> <歌曲ID> <音质>`

**运行方式**：
```bash
make url
# 或
./music_url <平台> <歌曲ID> <音质>
```

**示例**：
```bash
# 使用 QQ 音乐获取某首歌曲 128k 音质的链接
./music_url tx "001StgLm3NMZBG" 128k

# 使用酷我音乐获取 FLAC 音质的链接
./music_url kw "123456" flac
```

---

## 统一参数顺序

所有程序的参数顺序统一为：

| 功能 | 参数顺序 |
|------|---------|
| 搜索 | `<平台> <关键词>` |
| 下载 | `<平台> <歌曲ID> <音质> <输出目录>` |
| 搜索并下载 | `<平台> <关键词> <音质> <输出目录>` |
| 获取直链 | `<平台> <歌曲ID> <音质>` |

## 平台选项

| 平台代码 | 说明 |
|----------|------|
| `tx` | QQ 音乐 |
| `wy` | 网易云音乐 |
| `auto` | 自动选择（先 tx 后 wy） |

## 音质支持（音源）

### 酷我音乐 (kw)
- `128k` | `320k` | `flac` | `flac24bit` | `hires`

### 咪咕音乐 (mg)
- `128k` | `320k` | `flac` | `flac24bit` | `hires`

### 酷狗音乐 (kg)
- `128k` | `320k` | `flac` | `flac24bit` | `hires` | `atmos` | `master`

### QQ 音乐 (tx)
- `128k` | `320k` | `flac` | `flac24bit` | `hires` | `atmos` | `atmos_plus` | `master`

### 网易云音乐 (wy)
- `128k` | `320k` | `flac` | `flac24bit` | `hires` | `atmos` | `master`

## Makefile 命令

| 命令 | 描述 |
|------|------|
| `make` | 编译所有示例程序 |
| `make debug` | 编译调试版本 |
| `make search` | 编译并运行搜索程序 |
| `make download` | 编译并运行下载程序 |
| `make search-and-download` | 编译并运行搜索并下载程序 |
| `make url` | 编译并运行获取直链程序 |
| `make clean` | 清理编译文件 |
| `make clean-all` | 清理所有（包括 Rust 库） |
| `make help` | 显示帮助信息 |

## 头文件

`music.h` 包含了所有可用的 C 接口定义。
