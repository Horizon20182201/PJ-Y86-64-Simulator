#pragma once
#include "cpu.h"
#include <istream>

namespace y86 {
// 读取 stdin 的 .yo：装载字节到内存，设置初始 PC，为 MEM dump 标记触及块。
// bound=true 时，设置 upper = max_loaded + slack；越界访问报 ADR。
void load_yo(std::istream& in, CPU& cpu, bool bound = false,
             std::uint64_t slack = 65536);
}  // namespace y86
