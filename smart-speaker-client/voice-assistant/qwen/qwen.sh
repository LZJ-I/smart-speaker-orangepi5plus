#!/bin/bash

# -$# -eq 0 : 如果没有参数
if [ $# -eq 0 ]; then
    echo "请加上你的问题"
    exit 1
fi

# 从参数中获取用户消息
user_message=$1

# 执行脚本 ： 向qwen-plus模型发送请求
curl -X POST https://dashscope.aliyuncs.com/compatible-mode/v1/chat/completions \
-H "Authorization: Bearer sk-a90732c9453a41fc910a3baae3b71eb8" \
-H "Content-Type: application/json" \
-d '{
    "model": "qwen-plus",
    "messages": [
        {
            "role": "system",
            "content": "你是一个智能音箱助手，回复非常简洁、口语化，适合直接念出来。请遵守以下规则:1.每次回复尽量控制在1-2句话内。2.将语句中只要逗号和句号，不要用其他标点符号。3.不要列举项目符号(如1、2、3)。4.直接回答问题，不要复述用户问题或说根据您的问题。5.语气亲切自然，像朋友聊天一样。"
        },
        {
            "role": "user",
            "content": "'"$user_message"'"
        }
    ]
}'
 # 替换用户消息中的双引号为转义字符
