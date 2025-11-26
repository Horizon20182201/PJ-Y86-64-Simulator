// frontends/tui-ftxui/main.cpp
#include <atomic>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "worker.h"
#include "cpu.h"
#include "types.h"

#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>

using namespace ftxui;
using namespace y86;

struct Model {
  CPU cpu;
  bool loaded = false;
  std::set<u64> breakpoints;
  std::atomic<bool> running{false};
  std::mutex mtx;                 // 保护 cpu/mem
  std::vector<nlohmann::json> last_logs; // 最近若干步快照
  size_t mem_scroll = 0;          // 内存滚动位置（行）
  std::string last_msg;           // 状态栏消息

  void reset_runtime() {
    std::lock_guard<std::mutex> lk(mtx);
    running = false;
    last_logs.clear();
    mem_scroll = 0;
    last_msg.clear();
  }

  // 生成一步的快照，便于 UI 展示
  nlohmann::json snapshot_nolock() {
    nlohmann::json rec;
    rec["STAT"] = cpu.stat;
    rec["PC"]   = (s64)cpu.PC;
    rec["CC"]   = cpu.dump_cc();
    rec["REG"]  = cpu.dump_regs();
    rec["MEM"]  = cpu.dump_mem_nonzero();
    return rec;
  }

  // 单步执行（含快照与断点判断）
  void step_once() {
    std::lock_guard<std::mutex> lk(mtx);
    if (!loaded || cpu.stat != Stat::AOK) return;

    y86::step(cpu);  // 更新 CPU/MEM
    last_logs.push_back(snapshot_nolock());
    if (last_logs.size() > 200) last_logs.erase(last_logs.begin());

    // 命中断点？（取“执行后 PC”）
    if (breakpoints.count(cpu.PC)) {
      running = false;
      last_msg = "Hit breakpoint at PC=" + std::to_string((s64)cpu.PC);
    }
  }

  // 根据 mem 构建非零 8B 列表（每次渲染计算一次，数据量通常很小）
  std::vector<std::pair<u64, s64>> nonzero_qwords() {
    std::lock_guard<std::mutex> lk(mtx);
    std::vector<u64> bases;
    bases.reserve(cpu.mem.size());
    for (auto &kv : cpu.mem) bases.push_back(kv.first & ~7ULL);
    std::sort(bases.begin(), bases.end());
    bases.erase(std::unique(bases.begin(), bases.end()), bases.end());

    std::vector<std::pair<u64, s64>> out;
    for (u64 base : bases) {
      u64 v = 0;
      cpu.read8((s64)base, v);
      s64 sv = (s64)v;
      if (sv != 0) out.push_back({base, sv});
    }
    std::sort(out.begin(), out.end(), [](auto&a, auto&b){return a.first<b.first;});
    return out;
  }

  std::string stat_str() const {
    switch (cpu.stat) {
      case Stat::AOK: return "AOK(1)";
      case Stat::HLT: return "HLT(2)";
      case Stat::ADR: return "ADR(3)";
      case Stat::INS: return "INS(4)";
      default:  return "??";
    }
  }
};

// 十六/十进制地址解析
static bool parse_addr(const std::string& s, u64& out) {
  std::string t = s;
  // trim
  auto l = t.find_first_not_of(" \t"); if (l==std::string::npos) return false;
  auto r = t.find_last_not_of(" \t");  t = t.substr(l, r-l+1);
  int base = 10;
  if (t.size()>2 && (t[0]=='0') && (t[1]=='x' || t[1]=='X')) base = 16;
  try {
    out = std::stoull(t, nullptr, base);
    return true;
  } catch (...) { return false; }
}

int main(int argc, char** argv) {
  Model model;

  // 载入：命令行参数（可选）或 UI 里输入路径
  auto load_file = [&](const std::string& path)->std::string {
    std::ifstream fin(path);
    if (!fin) return "Open failed: " + path;
    model.reset_runtime();
    {
      std::lock_guard<std::mutex> lk(model.mtx);
      model.cpu = CPU{};
      load_yo(fin, model.cpu, false, 65536);
      model.loaded = (model.cpu.PC != 0 || !model.cpu.mem.empty());
    }
    return model.loaded ? ("Loaded: " + path) : "No code found in file.";
  };

  // ---------- UI 组件 ----------
  std::string path_input, bp_input;
  auto input_path = Input(&path_input, "path/to/program.yo");
  auto input_bp   = Input(&bp_input,   "breakpoint (e.g. 0x19)");

  // 运行线程：循环 step，刷新屏幕
  ScreenInteractive screen = ScreenInteractive::Fullscreen();
  std::thread runner;
  auto start_run = [&] {
    if (model.running || !model.loaded) return;
    model.running = true;
    model.last_msg = "Running...";
    runner = std::thread([&]{
      while (model.running && model.cpu.stat == Stat::AOK) {
        model.step_once();
        screen.PostEvent(Event::Custom); // 刷新
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
      screen.PostEvent(Event::Custom);
    });
  };
  auto stop_run = [&]{
    model.running = false;
    if (runner.joinable()) runner.join();
  };

  // 控件：载入/单步/运行/暂停/重载/断点增删
  auto btn_load = Button("Load (O)", [&]{
    if (path_input.empty()) { model.last_msg="Empty path."; return; }
    model.last_msg = load_file(path_input);
    screen.PostEvent(Event::Custom);
  });
  auto btn_step = Button("Step (S)", [&]{ stop_run(); model.step_once(); screen.PostEvent(Event::Custom); });
  auto btn_run  = Button("Run  (R)", [&]{ start_run(); });
  auto btn_stop = Button("Stop (T)", [&]{ stop_run(); model.last_msg="Stopped."; screen.PostEvent(Event::Custom); });
  auto btn_reload = Button("Reload", [&]{
    if (path_input.empty()) { model.last_msg="Empty path."; return; }
    model.last_msg = load_file(path_input);
    screen.PostEvent(Event::Custom);
  });
  auto btn_add_bp = Button("Add BP (B)", [&]{
    u64 a=0;
    if (parse_addr(bp_input, a)) {
      model.breakpoints.insert(a);
      model.last_msg = "Add BP @" + std::to_string((s64)a);
    } else {
      model.last_msg = "Bad address.";
    }
    screen.PostEvent(Event::Custom);
  });
  auto btn_clear_bp = Button("Clear BPs (C)", [&]{
    model.breakpoints.clear(); model.last_msg="Breakpoints cleared.";
    screen.PostEvent(Event::Custom);
  });

  // 顶部控制区容器
  auto control_row = Container::Horizontal({
    input_path, btn_load, btn_reload,
    input_bp, btn_add_bp, btn_clear_bp,
    btn_step, btn_run, btn_stop
  });

  // 渲染寄存器表
  auto render_regs = [&]{
    Elements rows;
    {
      std::lock_guard<std::mutex> lk(model.mtx);
      rows.push_back(text("Registers (dec)") | bold);
      for (int i=0;i<15;i++) {
        std::ostringstream ss;
        ss << std::left << std::setw(4) << reg_name(i) << " : "
           << std::right << std::setw(20) << model.cpu.R[i];
        rows.push_back(text(ss.str()));
      }
    }
    return vbox(std::move(rows)) | border;
  };

  // 渲染 CC/PC/STAT/断点
  auto render_status = [&]{
    std::lock_guard<std::mutex> lk(model.mtx);
    std::string s = "PC=" + std::to_string((s64)model.cpu.PC) +
                    "   STAT=" + model.stat_str() +
                    "   CC(ZF,SF,OF)=" + std::to_string(model.cpu.cc.ZF) + "," +
                    std::to_string(model.cpu.cc.SF) + "," + std::to_string(model.cpu.cc.OF) +
                    (model.running ? "   [RUNNING]" : "   [IDLE]");
    auto bp = text("Breakpoints: ") | color(Color::GrayLight);
    std::string bplist;
    bool first=true; for (auto a : model.breakpoints) {
      if (!first) bplist += ", ";
      first=false; bplist += std::to_string((s64)a);
    }
    return vbox({
      text(s) | bold,
      hbox({ bp, text(bplist.empty() ? "(none)" : bplist) })
    }) | border;
  };

  // 渲染内存非零块（分页/滚动）
  auto render_mem = [&]{
    auto items = model.nonzero_qwords();
    const size_t per_page = 20;
    if (model.mem_scroll > (items.size()>0?items.size()-1:0))
      model.mem_scroll = (items.size()>0?items.size()-1:0);
    size_t start = model.mem_scroll;
    size_t end   = std::min(start + per_page, items.size());

    Elements lines;
    lines.push_back(text("Memory (non-zero qwords, little-endian, signed)") | bold);
    for (size_t i=start;i<end;i++){
      std::ostringstream ss;
      ss << std::setw(16) << std::right << items[i].first << "  :  "
         << std::setw(20) << std::right << items[i].second;
      lines.push_back(text(ss.str()));
    }
    if (items.empty()) lines.push_back(text("(empty)"));
    return vbox(std::move(lines)) | border;
  };

  // 最近日志（最多 10 条）
  auto render_logs = [&]{
    Elements lines;
    lines.push_back(text("Last steps (JSON summary)") | bold);
    size_t n = model.last_logs.size();
    size_t from = (n>10 ? n-10 : 0);
    for (size_t i=from;i<n;i++) {
      const auto& j = model.last_logs[i];
      std::ostringstream ss;
      ss << "PC=" << j["PC"] << " STAT=" << j["STAT"]
         << " CC[ZF,SF,OF]=" << j["CC"]["ZF"] << "," << j["CC"]["SF"] << "," << j["CC"]["OF"];
      lines.push_back(text(ss.str()));
    }
    if (lines.size()==1) lines.push_back(text("(empty)"));
    return vbox(std::move(lines)) | border;
  };

  // 布局与渲染
  auto root = Container::Vertical({
    control_row,
    // 中间两列：寄存器 & 内存
    Container::Horizontal({}),
  });

  auto renderer = Renderer(root, [&]{
    auto top = hbox({
      control_row->Render()
    });

    auto mid = hbox({
      render_regs()     | flex,
      render_mem()      | flex,
      render_logs()     | flex
    });

    auto bottom = render_status();

    auto help = text(
      "[O] Load  [S] Step  [R] Run  [T] Stop  [B] Add BP  [C] Clear BP  "
      "[Up/Down] Scroll memory  [Q] Quit") | dim;

    auto msg = text(model.last_msg) | color(Color::GreenYellow);

    return vbox({
      top,
      separator(),
      mid | flex,
      separator(),
      bottom,
      separator(),
      hbox({ help, filler(), msg })
    });
  });

  // 键盘事件：快捷键与滚动
  renderer = CatchEvent(renderer, [&](Event e){
    if (e == Event::Character('q') || e == Event::Character('Q')) {
      stop_run();
      screen.Exit();
      return true;
    }
    if (e == Event::Character('o') || e == Event::Character('O')) {
      btn_load->OnEvent(Event::Return); return true;
    }
    if (e == Event::Character('s') || e == Event::Character('S')) {
      btn_step->OnEvent(Event::Return); return true;
    }
    if (e == Event::Character('r') || e == Event::Character('R')) {
      btn_run->OnEvent(Event::Return);  return true;
    }
    if (e == Event::Character('t') || e == Event::Character('T')) {
      btn_stop->OnEvent(Event::Return); return true;
    }
    if (e == Event::Character('b') || e == Event::Character('B')) {
      btn_add_bp->OnEvent(Event::Return); return true;
    }
    if (e == Event::Character('c') || e == Event::Character('C')) {
      btn_clear_bp->OnEvent(Event::Return); return true;
    }
    if (e == Event::ArrowDown) { if (model.mem_scroll < (1<<30)) model.mem_scroll++; return true; }
    if (e == Event::ArrowUp)   { if (model.mem_scroll>0) model.mem_scroll--; return true; }
    return false;
  });

  // 若命令行传了 .yo，自动载入
  if (argc >= 2) {
    path_input = argv[1];
    model.last_msg = load_file(path_input);
  }

  screen.Loop(renderer);
  stop_run();
  return 0;
}
