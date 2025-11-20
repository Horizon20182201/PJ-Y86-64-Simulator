#include "y86/loader.h"
#include <regex>
#include <string>
#include <vector>

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

}  // namespace y86
