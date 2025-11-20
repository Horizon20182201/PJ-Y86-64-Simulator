#include "y86/cpu.h"
using nlohmann::json;
namespace y86 {

bool CPU::read1(s64 a, u8& out) const {
    if (!check_addr(a, 1)) return false;
    auto it = mem.find((u64)a);
    out = (it == mem.end() ? 0 : it->second);
    return true;
}

bool CPU::write1(s64 a, u8 v) {
    if (!check_addr(a, 1)) return false;
    mem[(u64)a] = v;
    qword_touched.insert(align8((u64)a));
    return true;
}

bool CPU::read8(s64 a, u64& out) const {
    if (!check_addr(a, 8)) return false;
    out = 0;
    for (int i = 0; i < 8; i++) {
        auto it = mem.find((u64)a + i);
        u8 b = (it == mem.end() ? 0 : it->second);
        out |= (u64)b << (8 * i);
    }
    return true;
}

bool CPU::write8(s64 a, u64 v) {
    if (!check_addr(a, 8)) return false;
    for (int i = 0; i < 8; i++) {
        mem[(u64)a + i] = (u8)((v >> (8 * i)) & 0xFF);
    }
    qword_touched.insert(align8((u64)a));
    return true;
}

json CPU::dump_regs() const {
    json j = json::object();
    for (int i = 0; i < REG_NUM; i++) {
        j[reg_name(i)] = R[i];
    }
    return j;
}

json CPU::dump_cc() const {
    return json{{"OF", cc.OF}, {"SF", cc.SF}, {"ZF", cc.ZF}};
}

json CPU::dump_mem_nonzero() const {
    json j = json::object();
    for (auto base : qword_touched) {
        u64 raw = 0;
        // 这里的 read8 不会失败（base >= 0）
        const_cast<CPU*>(this)->read8((s64)base, raw);
        s64 val = (s64)raw;
        if (val != 0) j[std::to_string(base)] = val;
    }
    return j;
}

}  // namespace y86
