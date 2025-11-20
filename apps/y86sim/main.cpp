#include "y86/loader.h"
#include "y86/stepper.h"
#include <nlohmann/json.hpp>
#include <iostream>

using nlohmann::json;
using namespace y86;

int main() {
  std::ios::sync_with_stdio(false);
  std::cin.tie(nullptr);

  CPU cpu;
  // 如果希望对内存设置硬上界，第三个参数传 true（可按需要调整 slack）
  load_yo(std::cin, cpu, /*bound=*/false, /*slack=*/65536);

  const std::size_t LIMIT = 1'000'000; // 防死循环
  json out = json::array();
  for (std::size_t i=0; i<LIMIT; ++i) {
    json one = step(cpu);
    out.push_back(std::move(one));
    if (cpu.stat != Stat::AOK) break;
  }
  std::cout << out.dump(2) << "\n";
  return 0;
}
