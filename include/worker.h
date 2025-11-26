#pragma once
#include "cpu.h"
#include <istream>

namespace y86 {

void load_yo(std::istream& in, CPU& cpu, bool bound = false, std::uint64_t slack = 65536);

struct Decoded {
    u8 icode = 0, ifun = 0, rA = RNONE, rB = RNONE;
    u64 valC = 0, valP = 0;
    bool ok = true;
};

bool cond_true(const CC& c, u8 ifun);
Decoded fetch_and_decode(CPU& S);
nlohmann::json step(CPU& S);

}  // namespace y86
