#pragma once
#include "cpu.h"

namespace y86 {

struct Decoded {
    u8 icode = 0, ifun = 0, rA = RNONE, rB = RNONE;
    u64 valC = 0, valP = 0;
    bool ok = true;
};

bool cond_true(const CC& c, u8 ifun);
Decoded fetch_and_decode(CPU& S);
nlohmann::json step(CPU& S);  // 执行一步并返回本步的日志 JSON

}  // namespace y86
