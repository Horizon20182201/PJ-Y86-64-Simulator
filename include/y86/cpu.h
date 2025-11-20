#pragma once
#include "types.h"
#include <unordered_map>
#include <set>
#include <nlohmann/json.hpp>

namespace y86 {

struct CPU {
    s64 R[REG_NUM]{};
    u64 PC = 0;
    CC cc{};
    Stat stat = Stat::AOK;

    // byte-addressable memory (sparse)
    std::unordered_map<u64, u8> mem;
    // aligned 8B blocks touched (for compact MEM dump)
    std::set<u64> qword_touched;

    // optional hard bound (off by default)
    bool bounded = false;
    u64 mem_upper = 0;

    static inline u64 align8(u64 a) { return a & ~7ULL; }

    bool check_addr(s64 a, size_t len) const {
        if (a < 0) return false;
        if (!bounded) return true;
        return (u64)a + len - 1 <= mem_upper;
    }

    // byte/qword memory operations
    bool read1(s64 a, u8& out) const;
    bool write1(s64 a, u8 v);
    bool read8(s64 a, u64& out) const;
    bool write8(s64 a, u64 v);

    // dumps
    nlohmann::json dump_regs() const;
    nlohmann::json dump_cc() const;
    nlohmann::json dump_mem_nonzero() const;
};

}  // namespace y86
