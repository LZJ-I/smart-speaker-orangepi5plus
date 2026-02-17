# Qwen 示例程序

本目录包含基于阿里云 Qwen 大语言模型的智能助手示例程序。

## 功能特性

- 调用阿里云 Qwen-Plus 模型进行对话
- 自动解析 API 返回的 JSON 响应
- 简洁的接口设计

## 编译

```bash
make
```

## 运行

```bash
./qwen_test "你的问题"
```

## 程序结构

### 文件说明

- `sherpa_qwen.h`：Qwen 接口头文件
- `sherpa_qwen.c`：Qwen 接口实现
- `main.c`：测试程序
- `qwen.sh`：API 调用脚本
- `config.example.sh`：配置模板
- `config.sh`：实际配置文件（在 .gitignore 中）
- `Makefile`：编译配置
- `.gitignore`：Git 忽略配置

### 接口函数

```c
int init_sherpa_qwen(void);
int generate_qwen_response(const char *question, char *response, size_t response_len);
void cleanup_sherpa_qwen(void);
```

## 配置步骤

1. 复制示例配置文件：
   ```bash
   cp config.example.sh config.sh
   ```

2. 编辑 `config.sh`，填入你的 API Key：
   ```bash
   export QWEN_API_KEY="你的实际API密钥"
   ```

### 配置文件说明

- `config.example.sh`：配置模板（已上传）
- `config.sh`：实际配置文件（在 .gitignore 中，不会被上传）
  - `QWEN_API_KEY`：API 密钥
  - `QWEN_MODEL`：模型名称
  - `QWEN_SYSTEM_PROMPT`：系统提示词

## 依赖

- json-c 库（用于 JSON 解析）
- curl（用于 API 调用，在 qwen.sh 中）

## 信号处理

程序支持 `SIGINT`（Ctrl+C）信号，用于安全退出。
