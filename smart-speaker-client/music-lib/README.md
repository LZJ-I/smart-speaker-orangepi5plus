# Rust 音乐下载库（C 接口）

基于xx音源 API 开发的跨平台音乐下载库，提供完整的 C 语言接口。

## 项目简介

这是一个使用 Rust 语言开发的音乐下载库，支持从多个主流音乐平台下载不同音质的音乐文件。库提供了简洁的 C 接口，可以被 C/C++ 调用。

## 核心优势 ⭐

### 🔍 智能搜索与下载
- **多平台支持**：搜索支持 QQ 音乐和网易云音乐，下载支持所有主流音乐平台
- **音质自动降级**：从目标音质开始自动降级尝试，直到找到可用资源
- **歌曲自动切换**：最多尝试 5 首歌曲，确保下载成功率
- **自动文件命名**：按 `歌曲名-艺术家.后缀` 格式自动命名
- **获取直链功能**：支持直接获取音乐下载链接，无需下载文件

### 🛡️ 安全与可靠性
- **内存安全**：Rust 语言天然的内存安全特性，避免内存泄漏和段错误
- **完善入参检测**：在 Rust 端验证所有输入参数，提供友好的错误提示
  - 平台验证：搜索和下载平台都有明确的支持列表
  - 音质验证：验证音质是否有效，以及该平台是否支持该音质
  - 关键词/ID 验证：确保搜索关键词和歌曲 ID 不为空
- **完善错误处理**：多层级错误检测，API 错误和下载错误都能正确识别
- **错误响应过滤**：自动检测 JSON 错误响应，避免把错误当文件保存
- **C 接口安全**：完善的 FFI 边界处理，确保 C 语言调用安全

### ⚡ 高性能架构
- **Rust 核心**：零成本抽象，运行时开销极低
- **异步阻塞模型**：reqwest 阻塞 API，简单高效
- **流式下载**：边下载边保存，内存占用低
- **进度实时反馈**：支持下载进度回调

### 🔗 易集成设计
- **标准 C 接口**：完整的 FFI 实现，可被任何支持 C 调用的语言使用
- **清晰数据结构**：简单易懂的 C 结构体定义
- **统一参数风格**：所有函数参数顺序一致，易于记忆和使用
- **详细文档**：每个接口都有完整的文档注释

## 技术栈

| 组件 | 技术选型 | 说明 |
|------|---------|------|
| **核心语言** | Rust 2024 Edition | 内存安全、高性能 |
| **HTTP 客户端** | reqwest 0.12 | 阻塞 API，简单可靠 |
| **JSON 处理** | serde + serde_json | 类型安全的序列化/反序列化 |
| **FFI 接口** | Rust 标准库 | 提供 C 兼容接口 |
| **构建工具** | Cargo + Makefile | Rust 库和 C 示例分别构建 |
| **示例语言** | C11 | 提供完整的示例程序 |

## 项目结构

```
bishe/
├── Cargo.toml           # 项目配置
├── README.md            # 项目说明
├── src/
│   ├── api.rs           # API 交互模块（音源）
│   ├── downloader.rs    # 下载核心模块
│   ├── lib.rs           # C 接口定义
│   └── search.rs        # 搜索功能模块
├── examples/            # C 语言示例程序
│   ├── search/          # 搜索示例
│   ├── download/        # 下载示例
│   ├── search_and_download/  # 搜索并下载示例
│   ├── url/             # 获取直链示例
│   ├── music.h          # C 头文件
│   ├── Makefile         # 构建脚本
│   └── README.md        # 示例说明
└── downloads/           # 下载文件目录
```

## ⚠️ 重要说明

- **下载功能**：支持所有平台
  - `kw` - 酷我音乐
  - `mg` - 咪咕音乐
  - `kg` - 酷狗音乐
  - `tx` - QQ 音乐
  - `wy` - 网易云音乐

- **搜索功能**：仅支持
  - `tx` - QQ 音乐
  - `wy` - 网易云音乐
  - `auto` - 自动选择（先 tx 后 wy）

## 编译与使用

### 1. 编译 Rust 库

```bash
# 编译调试版本
cargo build

# 编译发布版本（推荐）
cargo build --release
```

编译完成后，库文件会生成在 `target/release/` 目录。

### 2. 编译并运行 C 示例

```bash
cd examples
make
make help  # 查看更多命令
```

详细的示例使用说明请参考 [examples/README.md](./examples/README.md)。

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

// 下载音乐（指定完整路径）⭐
music_result_t music_download_with_path(
    const char* source,
    const char* song_id,
    const char* quality,
    const char* output_path, // 完整路径
    progress_callback_t callback,
    void* user_data
);
```

### 获取直链相关 ⭐

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
