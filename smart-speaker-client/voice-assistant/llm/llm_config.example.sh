#!/bin/bash

# LLM 配置文件 - 示例模板
# 使用方法：
# 1. 复制此文件为 llm_config.sh
# 2. 填入你的 API Key 和其他配置
# 3. llm_config.sh 已在 .gitignore 中，不会被上传

# API Key (请填入你的 API Key)
export LLM_API_KEY="your-api-key-here"

# 模型名称
export LLM_MODEL="qwen-plus"

# 系统提示词
export LLM_SYSTEM_PROMPT="你是一个智能音箱助手，回复非常简洁、口语化，适合直接念出来。请遵守以下规则:1.每次回复尽量控制在1-2句话内。2.将语句中只要逗号和句号，不要用其他标点符号。3.不要列举项目符号(如1、2、3)。4.直接回答问题，不要复述用户问题或说根据您的问题。5.语气亲切自然，像朋友聊天一样。"
