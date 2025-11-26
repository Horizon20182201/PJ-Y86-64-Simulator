#!/bin/bash

# 检查是否提供了输入文件
if [ $# -eq 0 ]; then
    echo "Usage: $0 <input_file.mc>"
    exit 1
fi

INPUT_FILE="$1"

# 检查输入文件是否存在
if [ ! -f "$INPUT_FILE" ]; then
    echo "Error: File '$INPUT_FILE' not found"
    exit 1
fi

# 从输入文件路径中提取文件名（去掉路径和扩展名）
# 例如: examples/cmp.mc -> cmp
BASENAME=$(basename "$INPUT_FILE" .mc)
OUTPUT_FILE="yo/${BASENAME}.yo"

# 确保 yo 目录存在
mkdir -p yo

# 运行编译器生成 .yo 文件
python3 minic_to_y86.py "$INPUT_FILE" > "$OUTPUT_FILE"

# 检查编译是否成功
if [ $? -ne 0 ]; then
    echo "Error: Failed to compile $INPUT_FILE"
    exit 1
fi

# 运行 y86sim（假设从 minic 目录运行，build 在上级目录）
../build/y86sim < "$OUTPUT_FILE" > out.json

# 显示结果
echo "Generated .yo file: '$OUTPUT_FILE'"
jq '.[-1].REG.rax' out.json