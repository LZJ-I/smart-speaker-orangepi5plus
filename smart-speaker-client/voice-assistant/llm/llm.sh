#!/bin/bash

# 加载配置文件
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/llm_config.sh"

LLM_LOG_DIR="$SCRIPT_DIR/../../data/llm"
mkdir -p "$LLM_LOG_DIR"
exec 3>>"$LLM_LOG_DIR/curl.log"

# -$# -eq 0 : 如果没有参数
if [ $# -eq 0 ]; then
    echo "请加上你的问题"
    exit 1
fi

# 从参数中获取用户消息
user_message=$1

# 执行脚本 ： 向qwen-plus模型发送请求
# 注意：stdout 必须是纯 JSON（供 C 端 json-c 解析）；时间戳只写入 data/llm/curl.log
echo "---- $(date -Iseconds) ----" >&3
curl -sS -X POST https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions \
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
}' | tee /dev/fd/3
echo "" >&3
