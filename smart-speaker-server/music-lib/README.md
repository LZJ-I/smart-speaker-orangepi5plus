# music-lib（Rust，C 接口）

> 随 **smart-speaker-server** 一起部署：在仓库 **`smart-speaker-server`** 下执行 `make` 会 `cargo build --release` 生成 `target/release/libmusic_downloader.so`，由主程序加载。主服务配置见 [上级 README.md](../README.md) 的 `data/config/*.toml`。

## 应用做什么

为服务端提供**远程搜索分页**与**单条取链**等能力，供 TCP 业务（如 `list_music` 带关键词、`get_play_url`）调用。下载类接口也可给 `examples/` 独立小程序使用。

## 项目结构

```
music-lib/
├── Cargo.toml
├── src/           # Rust 实现与 C FFI
├── examples/      # C 示例，见 examples/README.md
└── target/release/libmusic_downloader.so   # release 构建产物
```

## 能力与接口摘要

| 能力 | C 符号 | 说明 |
|------|--------|------|
| 搜索分页 | `music_search_page` | QQ/网易云聚合（不消耗音源 Key） |
| 单条取链 | `music_get_url` | 调用第三方音源 API，**依赖环境变量 `SMART_SPEAKER_MUSIC_API_KEY`** |
| 是否已配置 Key | `music_api_configured` | 未配置则取链与 `music_resolve_keyword` / `music_search_first_url` 失败 |
| 关键词→首条+单链 | `music_resolve_keyword` / `music_free_resolve_result` | 一次搜索（1 条）+ 一次取链，返回元数据与 `play_url` |
| 仅首条 URL | `music_search_first_url` | 同上，只返回 URL 字符串 |
| 下载 | `music_download` 等 | 需有效取链 URL |

示例：`examples/search_first_url/music_search_first_url`（需 `export SMART_SPEAKER_MUSIC_API_KEY=...` 后运行）。

## 平台与搜索范围

- **下载**：`kw`、`mg`、`kg`、`tx`、`wy` 等（见下文音质表）。
- **搜索分页**：`tx`、`wy`、`auto`（先 tx 后 wy）。

## 编译与使用

### Rust 库

```bash
cargo build --release
```

### C 示例

```bash
cd examples
make
make help
```

详见 [examples/README.md](./examples/README.md)。

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

## C 接口文档

### 数据结构

```c
// 音乐信息
typedef struct {
    char id[64];           // 歌曲 ID
    char name[128];        // 歌曲名
    char artist[128];      // 歌手名
    char album[128];       // 专辑名
    char source[8];        // 平台
    char* url;             // 下载链接（可选）
} music_info_t;

// 搜索结果
typedef struct {
    music_info_t* results; // 结果数组
    size_t count;          // 数量
} music_search_result_t;

// 错误码
typedef enum {
    Ok = 0,               // 成功
    InvalidParam = 1,     // 无效参数
    ApiError = 2,         // API 错误
    DownloadError = 3     // 下载错误
} music_result_t;

// 进度回调
typedef void (*progress_callback_t)(
    int64_t downloaded,
    int64_t total,
    void* user_data
);
```

### 搜索相关

```c
// 搜索音乐
music_result_t music_search(
    const char* keyword,    // 搜索关键词
    const char* platform,   // 平台: tx, wy, auto
    music_search_result_t* result
);

// 释放搜索结果
void music_free_search_result(music_search_result_t* result);
```

### 下载相关

```c
// 下载音乐（自动命名）
music_result_t music_download(
    const char* source,      // 音乐源
    const char* song_id,     // 歌曲 ID
    const char* quality,     // 音质
    const char* output_dir,  // 输出目录
    progress_callback_t callback,
    void* user_data
);

// 下载音乐（指定完整路径）
music_result_t music_download_with_path(
    const char* source,
    const char* song_id,
    const char* quality,
    const char* output_path, // 完整路径
    progress_callback_t callback,
    void* user_data
);
```

### 获取直链相关

```c
// 获取单个歌曲的下载链接
// 返回值：成功时返回下载链接（需要调用 music_free_string 释放），失败时返回 NULL
char* music_get_url(
    const char* source,      // 音乐源
    const char* song_id,     // 歌曲 ID
    const char* quality      // 音质
);
```

### 工具函数

```c
// 获取文件扩展名
char* music_get_extension(const char* quality);

// 释放字符串
void music_free_string(char* s);
```

## 注意事项

1. 本项目仅供学习和研究使用，请勿用于商业用途
2. 请遵守相关法律法规，尊重音乐版权
3. 确保网络连接正常
4. 使用 C 接口时注意内存管理，及时释放资源

## 许可证

MIT License
