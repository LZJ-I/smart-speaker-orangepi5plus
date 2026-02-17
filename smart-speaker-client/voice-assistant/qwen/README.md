# Qwen 示例程序

本目录包含基于阿里云 Qwen 大语言模型的智能助手示例程序。

## 功能特性

- 调用阿里云 Qwen-Plus 模型进行对话
- 自动解析 API 返回的 JSON 响应
- 将回复内容发送到 TTS 管道进行语音播报
- 配置为智能音箱助手风格，回复简洁口语化

## 编译

```bash
make
```

## 运行

```bash
./qwen "你的问题"
```

## 程序说明

### 工作流程

1. 接收用户输入的问题
2. 调用 `qwen.sh` 脚本访问阿里云 Qwen API
3. 解析 JSON 响应，提取回复内容
4. 将回复内容写入 TTS 管道 `../../fifo/tts_fifo`
5. TTS 模块读取管道内容并进行语音播报

### qwen.sh 配置

脚本中包含以下配置：
- **API 地址**：阿里云 DashScope 兼容接口
- **模型**：qwen-plus
- **系统提示词**：配置为智能音箱助手风格
  - 回复简洁（1-2句话）
  - 只使用逗号和句号
  - 不使用项目符号
  - 直接回答问题
  - 语气亲切自然

### 依赖

- json-c 库（用于 JSON 解析）
- curl（用于 API 调用，在 qwen.sh 中）

## 错误码

| 错误码 | 说明 |
|-------|------|
| 0 | 成功 |
| 1 | 请加上你的问题 |
| 2 | 执行sh命令失败 |
| 3 | JSON解析失败 |
| 4 | 未找到choices字段 |
| 5 | 未找到message字段 |
| 6 | 未找到content字段 |
| 7 | 打开tts_fifo失败 |
| 8 | 写入tts_fifo失败 |

## 配置文件

### 配置步骤

1. 复制示例配置文件：
   ```bash
   cp config.example.sh config.sh
   ```

2. 编辑 `config.sh`，填入你的 API Key：
   ```bash
   export QWEN_API_KEY="你的实际API密钥"
   ```

### 文件说明

- `config.example.sh`：配置模板（已上传）
- `config.sh`：实际配置文件（在 .gitignore 中，不会被上传）
  - `QWEN_API_KEY`：API 密钥
  - `QWEN_MODEL`：模型名称
  - `QWEN_SYSTEM_PROMPT`：系统提示词
- `qwen.sh`：API 调用脚本，从 config.sh 加载配置
- `.gitignore`：Git 忽略配置，保护 config.sh 不被上传
