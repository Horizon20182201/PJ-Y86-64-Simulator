#include "worker.h"

#include <regex>
#include <string>
#include <vector>

using nlohmann::json;

namespace y86 {

static std::vector<u8> hex_to_bytes(const std::string& s_in) {
    std::string s;
    s.reserve(s_in.size());
    for (char c : s_in) {
        if (!isspace((unsigned char)c)) s.push_back(c);
    }
    std::vector<u8> out;
    if (s.size() % 2 != 0) return out;
    out.reserve(s.size() / 2);
    for (size_t i = 0; i < s.size(); i += 2) {
        out.push_back((u8)strtoul(s.substr(i, 2).c_str(), nullptr, 16));
    }
    return out;
}

void load_yo(std::istream& in, CPU& cpu, bool bound, std::uint64_t slack) {
    std::string line;
    std::regex re(R"(0x([0-9a-fA-F]+):\s*([0-9a-fA-F\s]*))");

    u64 entry = ~0ULL, maxaddr = 0;
    while (std::getline(in, line)) {
        std::smatch m;
        if (!std::regex_search(line, m, re)) continue;
        u64 addr = strtoull(m[1].str().c_str(), nullptr, 16);
        auto bytes = hex_to_bytes(m[2].str());
        if (entry == ~0ULL) entry = addr;
        if (addr < entry) entry = addr;
        for (size_t i = 0; i < bytes.size(); ++i) {
            cpu.write1((s64)addr + (s64)i, bytes[i]);
            if (addr + i > maxaddr) maxaddr = addr + (u64)i;
        }
    }
    cpu.PC = (entry == ~0ULL ? 0 : entry);
    cpu.bounded = bound;
    if (bound) cpu.mem_upper = maxaddr + slack;
}

bool cond_true(const CC& c, u8 ifun) {
    switch (ifun) {
        case 0:
            return true;  // ALWAYS(jmp/rrmovq)
        case 1:
            return (c.SF ^ c.OF) || c.ZF;  // le
        case 2:
            return (c.SF ^ c.OF);  // l
        case 3:
            return c.ZF;  // e
        case 4:
            return !c.ZF;  // ne
        case 5:
            return !(c.SF ^ c.OF);  // ge
        case 6:
            return !(c.SF ^ c.OF) && !c.ZF;  // g
        default:
            return false;
    }
}

Decoded fetch_and_decode(CPU& S) {
    Decoded d;
    u8 b0 = 0;
    if (!S.read1((s64)S.PC, b0)) {
        S.stat = Stat::ADR;
        d.ok = false;
        return d;
    }
    d.icode = (b0 >> 4) & 0xF;
    d.ifun = b0 & 0xF;
    u64 pc = S.PC + 1;

    auto need_reg = [&](u8 ic) {
        return ic == (u8)Icode::RRMOVQ || ic == (u8)Icode::IRMOVQ ||
               ic == (u8)Icode::RMMOVQ || ic == (u8)Icode::MRMOVQ ||
               ic == (u8)Icode::OPQ || ic == (u8)Icode::PUSHQ ||
               ic == (u8)Icode::POPQ;
    };
    auto need_valC = [&](u8 ic) {
        return ic == (u8)Icode::IRMOVQ || ic == (u8)Icode::RMMOVQ ||
               ic == (u8)Icode::MRMOVQ || ic == (u8)Icode::JXX ||
               ic == (u8)Icode::CALL;
    };

    if (d.icode > (u8)Icode::POPQ) {
        S.stat = Stat::INS;
        d.ok = false;
        return d;
    }

    if (need_reg(d.icode)) {
        u8 rb = 0;
        if (!S.read1((s64)pc, rb)) {
            S.stat = Stat::ADR;
            d.ok = false;
            return d;
        }
        d.rA = (rb >> 4) & 0xF;
        d.rB = rb & 0xF;
        pc += 1;
    }
    if (need_valC(d.icode)) {
        u64 v = 0;
        if (!S.read8((s64)pc, v)) {
            S.stat = Stat::ADR;
            d.ok = false;
            return d;
        }
        d.valC = v;
        pc += 8;
    }
    d.valP = pc;
    return d;
}

static inline void set_cc_opq(CPU& S, s64 a, s64 b, s64 r, u8 ifun) {
    S.cc.ZF = (r == 0);
    S.cc.SF = (r < 0);
    if (ifun == 0) {  // add
        S.cc.OF = ((a < 0) == (b < 0)) && ((r < 0) != (a < 0));
    } else if (ifun == 1) {  // sub: b - a
        S.cc.OF = ((b < 0) != (a < 0)) && ((r < 0) != (b < 0));
    } else {
        S.cc.OF = 0;
    }
}

nlohmann::json step(CPU& S) {
    Decoded d = fetch_and_decode(S);
    json log;

    // 取指阶段出错（ADR/INS）
    if (!d.ok) {
        log["STAT"] = (int)S.stat;
        log["PC"] = (std::int64_t)S.PC;
        log["CC"] = S.dump_cc();
        log["REG"] = S.dump_regs();
        log["MEM"] = S.dump_mem_nonzero();
        return log;
    }

    auto R = [&](u8 id) -> s64& {
        static s64 dummy = 0;
        return (id == RNONE) ? dummy : S.R[id];
    };

    s64 valA = 0, valB = 0;
    u64 valE = 0, valM = 0;
    bool Cnd = false;

    // Decode
    switch ((Icode)d.icode) {
        case Icode::RRMOVQ:
        case Icode::OPQ:
        case Icode::RMMOVQ:
        case Icode::PUSHQ:
            valA = R(d.rA);
            break;
        case Icode::POPQ:
        case Icode::RET:
            valA = R(4);  // rsp
            break;
        default:
            break;
    }
    switch ((Icode)d.icode) {
        case Icode::RMMOVQ:
        case Icode::MRMOVQ:
        case Icode::OPQ:
        case Icode::PUSHQ:
        case Icode::POPQ:
        case Icode::CALL:
        case Icode::RET:
            valB = R(d.rB == RNONE ? 4 : d.rB);
            break;
        default:
            break;
    }

    // Execute
    switch ((Icode)d.icode) {
        case Icode::OPQ: {
            s64 a = valA, b = valB, r = 0;
            switch (d.ifun) {
                case 0:
                    r = b + a;
                    break;
                case 1:
                    r = b - a;
                    break;
                case 2:
                    r = b & a;
                    break;
                case 3:
                    r = b ^ a;
                    break;
                default:
                    S.stat = Stat::INS;
                    break;
            }
            if (S.stat == Stat::INS) goto FINISH;
            valE = (u64)r;
            set_cc_opq(S, a, b, r, d.ifun);
        } break;
        case Icode::RMMOVQ:
        case Icode::MRMOVQ:
            valE = (u64)((s64)valB + (s64)(s64)d.valC);
            break;
        case Icode::CALL:
        case Icode::PUSHQ:
            valE = (u64)((s64)valB - 8);
            S.R[4] = (s64)valE;
            break;
        case Icode::RET:
        case Icode::POPQ:
            valE = (u64)((s64)valB + 8);
            break;
        default:
            break;
    }

    // Memory
    switch ((Icode)d.icode) {
        case Icode::RMMOVQ:
            if (!S.write8((s64)valE, (u64)valA)) {
                S.stat = Stat::ADR;
                goto FINISH;
            }
            break;
        case Icode::MRMOVQ:
            if (!S.read8((s64)valE, valM)) {
                S.stat = Stat::ADR;
                goto FINISH;
            }
            break;
        case Icode::CALL:
            if (!S.write8((s64)valE, d.valP)) {
                S.stat = Stat::ADR;
                goto FINISH;
            }
            break;
        case Icode::PUSHQ:
            if (!S.write8((s64)valE, (u64)valA)) {
                S.stat = Stat::ADR;
                goto FINISH;
            }
            break;
        case Icode::RET:
            if (!S.read8((s64)valB, valM)) {
                S.stat = Stat::ADR;
                goto FINISH;
            }
            break;
        case Icode::POPQ:
            if (!S.read8((s64)valB, valM)) {
                S.stat = Stat::ADR;
                goto FINISH;
            }
            break;
        default:
            break;
    }

    // Write back
    if ((Icode)d.icode == Icode::RRMOVQ) {
        if (d.ifun == 0 /*ALWAYS*/) {
            R(d.rB) = valA;
        } else {
            Cnd = cond_true(S.cc, d.ifun);
            if (Cnd) R(d.rB) = valA;
        }
    } else if ((Icode)d.icode == Icode::IRMOVQ) {
        R(d.rB) = (s64)d.valC;
    } else if ((Icode)d.icode == Icode::OPQ) {
        R(d.rB) = (s64)valE;
    } else if ((Icode)d.icode == Icode::MRMOVQ) {
        R(d.rA) = (s64)valM;
    } else if ((Icode)d.icode == Icode::CALL ||
               (Icode)d.icode == Icode::PUSHQ) {
        S.R[4] = (s64)valE;
    } else if ((Icode)d.icode == Icode::RET ||
               (Icode)d.icode == Icode::POPQ) {
        S.R[4] = (s64)valE;
        if ((Icode)d.icode == Icode::POPQ) R(d.rA) = (s64)valM;
    }

    // PC update / halt
    switch ((Icode)d.icode) {
        case Icode::JXX:
            Cnd = cond_true(S.cc, d.ifun);
            S.PC = (d.ifun == 0 || Cnd) ? d.valC : d.valP;
            break;
        case Icode::CALL:
            S.PC = d.valC;
            break;
        case Icode::RET:
            S.PC = valM;
            break;
        case Icode::HALT:
            S.stat = Stat::HLT;
            break;
        default:
            S.PC = d.valP;
            break;
    }

FINISH:
    log["STAT"] = (int)S.stat;
    log["PC"] = (std::int64_t)S.PC;
    log["CC"] = S.dump_cc();
    log["REG"] = S.dump_regs();
    log["MEM"] = S.dump_mem_nonzero();
    return log;
}

}  // namespace y86
