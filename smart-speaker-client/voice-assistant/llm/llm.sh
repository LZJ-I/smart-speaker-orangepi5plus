#!/bin/bash

# 加载配置文件
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/llm_config.sh"

# -$# -eq 0 : 如果没有参数
if [ $# -eq 0 ]; then
    echo "请加上你的问题"
    exit 1
fi

# 从参数中获取用户消息
user_message=$1

# 执行脚本 ： 向qwen-plus模型发送请求
curl -X POST https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions \
-H "Authorization: Bearer $LLM_API_KEY" \
-H "Content-Type: application/json" \
-d '{
    "model": "'"$LLM_MODEL"'",
    "messages": [
        {
            "role": "system",
            "content": "'"$LLM_SYSTEM_PROMPT"'"
        },
        {
            "role": "user",
            "content": "'"$user_message"'"
        }
    ]
}'
