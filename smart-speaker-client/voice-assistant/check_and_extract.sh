#!/bin/bash

SHERPA_DIR="../3rdparty/sherpa-onnx"
JNI_DIR="${SHERPA_DIR}/sherpa-onnx-v1.12.25-linux-aarch64-jni"
SHARED_CPU_DIR="${SHERPA_DIR}/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu"
JNI_TAR="${SHERPA_DIR}/sherpa-onnx-v1.12.25-linux-aarch64-jni.tar.bz2"
SHARED_CPU_TAR="${SHERPA_DIR}/sherpa-onnx-v1.12.25-linux-aarch64-shared-cpu.tar.bz2"

echo "检查 sherpa-onnx 文件检查工具"
echo "=============================="

needs_jni=0
needs_shared_cpu=0

if [ ! -d "${JNI_DIR}/include" ]; then
    echo "❌ 未找到头文件目录: ${JNI_DIR}/include"
    needs_jni=