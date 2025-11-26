#pragma once
#include <cstdint>

namespace y86 {

using u8 = std::uint8_t;
using u64 = std::uint64_t;
using s64 = std::int64_t;

constexpr int REG_NUM = 15;
constexpr u8 RNONE = 0xF;

enum class Stat : int { AOK = 1, HLT = 2, ADR = 3, INS = 4 };

enum class Icode : u8 {
    HALT = 0x0,
    NOP = 0x1,
    RRMOVQ = 0x2,
    IRMOVQ = 0x3,
    RMMOVQ = 0x4,
    MRMOVQ = 0x5,
    OPQ = 0x6,
    JXX = 0x7,
    CALL = 0x8,
    RET = 0x9,
    PUSHQ = 0xA,
    POPQ = 0xB
};

enum class OPfun : u8 { ADDQ = 0x0, SUBQ = 0x1, ANDQ = 0x2, XORQ = 0x3 };
enum class Cfun : u8 {
    ALWAYS = 0x0,
    LE = 0x1,
    L = 0x2,
    E = 0x3,
    NE = 0x4,
    GE = 0x5,
    G = 0x6
};

struct CC {
    int ZF = 1, SF = 0, OF = 0;
};

inline const char* reg_name(int id) {
    static const char* names[REG_NUM] = {
        "rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
        "r8",  "r9",  "r10", "r11", "r12", "r13", "r14"
    };
    return (0 <= id && id < REG_NUM) ? names[id] : "??";
}

}  // namespace y86
