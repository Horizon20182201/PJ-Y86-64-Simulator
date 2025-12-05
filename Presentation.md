---
marp: true
title: Y86-64 Simulator 核心实现汇报
paginate: true
theme: default
---

<!-- _class: lead -->

# Y86-64 Simulator 核心实现汇报

**课程：计算机系统基础 · Y86-64 模拟器**

- 作者：刘恩照
- GitHub账号：Horizon20182201
- 语言：C++17  

---

# 目标与整体思路

## 项目目标

- 实现 **Y86-64 指令集** 的顺序执行模拟器
- 支持从 `.yo` 文件加载程序、单步执行，并提供**JSON 日志输出**
- 编写了一个性能评测脚本 bench_y86.py
- 利用 FTXUI 设计前端 TUI
- 实现了一个 Mini-C → Y86 编译器

> 搭建一个从 ISA 到 调试器 的较为完整的实验平台

---

## 核心设计思路

1. 把模拟器抽象成一个 `CPU` 状态对象，保存 Registers / CC / PC / Memory 等信息
2. 所有“硬件行为”封装为 `CPU` 的成员函数（读写内存、转储状态等）
3. 把 **单步执行逻辑** 集中在 `step(CPU&)` 中，实现“取指→译码→执行→访存→写回→PC 更新”
4. 使用 `nlohmann::json` 输出每步状态，方便调试与展示

---

# ISA 与类型建模（`types.h`）

## 基本类型与寄存器

- 自定义整型别名：`u8 / u64 / s64`，统一位宽
  - 避免直接使用 `int`、`long` 带来的平台差异
- 寄存器数量 `REG_NUM = 15`，用 `enum` 隐含 RAX…R14 的编号
- 特殊寄存器编号：
  - `4` → `rsp`
  - `RNONE = 0xF` 表示“无寄存器”

---

## 状态枚举与指令种类

- 状态 `Stat`：`AOK / HLT / ADR / INS` 表示正常、停机、地址错、非法指令
- 指令枚举 `Icode`：`HALT / NOP / ... / POPQ` 覆盖 Y86 基本指令集
- 算术运算 `OPfun`、条件码 `Cfun` 独立枚举，便于扩展与分发

> **把所有 ISA 元信息集中在头文件**，后续扩展指令时只需在此处添加枚举 + 在执行阶段补充逻辑。

---

# CPU 状态与内存模型（`cpu.h` / `cpu.cpp`）

## CPU 结构体

```cpp
struct CPU {
    s64 R[REG_NUM]{};
    u64 PC = 0;
    CC cc{};
    Stat stat = Stat::AOK;

    Mem mem;
    std::set<u64> qword_touched;
    bool bounded = false;
    u64 mem_upper = 0;
};
````

---

* `R[]`：通用寄存器数组，统一用 `s64` 表示
* `PC`：指令计数器
* `cc`：条件码（ZF / SF / OF）
* `stat`：当前状态
* `mem`：`std::unordered_map<u64,u8>` 实现的 **稀疏内存**
* `qword_touched`：记录被写过的 8B 对齐地址，用于紧凑内存转储

---

## 内存操作封装

* `read1 / write1`：读写单字节
* `read8 / write8`：小端读写 8 字节，循环 8 次操作 `mem` 映射 
* 所有读写先调用 `check_addr`，统一进行：

  * 地址非负检查
  * 可选上界 `mem_upper` 检查（用于实验中的“内存边界”）

> 用 C++ 容器实现 **稀疏内存**，无需预先分配大数组，同时仍然提供“字节寻址 + 8 字节读写”。

---

# .yo 文件加载与内存边界（`worker.cpp`）

## `.yo` 加载流程

函数：`void load_yo(std::istream& in, CPU& cpu, bool bound, u64 slack)` 

1. 正则匹配每一行：`0xADDR: BYTES...`
2. 解析地址 `addr` 和十六进制字节串 → `hex_to_bytes`
3. 调用 `cpu.write1()` 将字节写入内存

---

4. 维护：

   * `entry`：所有指令中最小的地址（程序入口）
   * `maxaddr`：最后一个写入字节的最大地址
5. 设置：

   * `cpu.PC = entry`（没有任何内容则 PC=0）
   * `cpu.bounded = bound`，`cpu.mem_upper = maxaddr + slack`

---

## 思路

* 用正则 + 字符串处理，开发效率较高、逻辑清晰
* 支持“**内存上界 + slack**”：

  * 模拟有限内存，触发“越界访问（ADR）”
  * 方便捕获诸如 `rsp` 指针错误等 bug

---

# 取指与译码（`Decoded` / `fetch_and_decode`）

## 译码结构体

```cpp
struct Decoded {
    u8 icode = 0, ifun = 0, rA = RNONE, rB = RNONE;
    u64 valC = 0, valP = 0;
    bool ok = true;
};
```

* 存放一次指令译码所需的全部信息
* `valP`：本条指令“正常执行”后的下一条 PC
* `ok=false` 时表示译码阶段已经发现错误（ADR/INS）

---

## `fetch_and_decode(CPU&)` 核心流程

1. 从 `PC` 读取首字节 → 取高 4 位 `icode`，低 4 位 `ifun`
2. 根据 `icode` 判断是否需要：

   * 寄存器字节（`rA:rB`）
   * 立即数 `valC`（8 字节）
3. 逐步推进本地的 `pc` 指针，最终写入 `d.valP`
4. 出错处理：

   * `read1/read8` 失败 → `stat=ADR`
   * `icode` 超出范围 → `stat=INS`

> **取指和译码统一封装**，上层 `step()` 只需要看 `Decoded`，不会直接操作 `CPU.mem`。

---

# 单步执行流程概览（`step(CPU&)`）

## 函数签名与返回值

```cpp
nlohmann::json step(CPU& S);
```

* 输入：CPU 当前状态
* 输出：本步执行后的状态快照（JSON）：

  * `STAT`：当前 `Stat`
  * `PC`：下一条指令地址
  * `CC`：条件码
  * `REG`：各寄存器值
  * `MEM`：非零内存的压缩表示

---

## 高层结构

1. 调用 `fetch_and_decode`
2. 若取指/译码失败 → 直接打日志返回
3. 否则依次执行：

   * Decode 阶段：计算 `valA`、`valB`
   * Execute 阶段：ALU 计算、更新 CC
   * Memory 阶段：读写内存
   * Writeback 阶段：回写寄存器 / `rsp`
   * PC Update 阶段：更新 `PC` 或设置 `HLT`

> 逻辑上模拟了 **五阶段** 的行为，但实现上仍然是 **顺序执行的一次性函数**，结构清晰易读。

---

# Decode 阶段细节：寄存器读

```cpp
auto R = [&](u8 id) -> s64& {
    static s64 dummy = 0;
    return (id == RNONE) ? dummy : S.R[id];
};
```

* 返回寄存器的 **引用**，便于统一读/写
* 对 `RNONE` 返回一个静态 `dummy`，避免越界访问

---

寄存器选择规则（简化）：

* `valA`：

  * `RRMOVQ / OPQ / RMMOVQ / PUSHQ` → `R(rA)`
  * `RET / POPQ` → `R(rsp)`
* `valB`：

  * 访存 / 算术 / CALL / RET / PUSH / POP 统一读取 `rB` 或 `rsp`：

    * `valB = R(d.rB == RNONE ? 4 : d.rB);`

> 通过 `RNONE` 和一个 lambda 把寄存器读写统一抽象成“函数调用”，避免大量 `switch`。

---

# Execute & Memory & Writeback

## Execute 阶段

* `OPQ`：

  * 根据 `ifun` 执行 `add/sub/and/xor`，使用 `set_cc_opq` 更新条件码
* `RMMOVQ/MRMOVQ`：

  * `valE = valB + valC` 作为内存地址
* `CALL/PUSHQ`：

  * `valE = valB - 8`，同时立即更新 `rsp`

---

* `RET/POPQ`：

  * `valE = valB + 8`（新栈顶），但访存使用旧 `rsp`

## Memory 阶段

* 根据指令类型决定是 `read8` 还是 `write8`，失败统一置 `ADR` 并跳转 `FINISH`：

  * `RMMOVQ`：`write8(valE, valA)`
  * `MRMOVQ`：`read8(valE, valM)`
  * `CALL`：`write8(valE, valP)`
  * `PUSHQ`：`write8(valE, valA)`
  * `RET/POPQ`：`read8(valB, valM)`（从旧 `rsp` 读）

---

## Writeback 阶段

* `RRMOVQ / IRMOVQ / OPQ / MRMOVQ`：回写到 `rB` 或 `rA`
* `CALL/PUSHQ`：写回 `rsp = valE`
* `RET/POPQ`：

  * `rsp = valE`（弹栈）
  * `POPQ` 额外 `R(rA) = valM`

---

# PC 更新与条件跳转

## 条件逻辑 `cond_true`

```cpp
bool cond_true(const CC& c, u8 ifun) {
    switch (ifun) {
        case 0: return true;           // ALWAYS
        case 1: return (c.SF ^ c.OF) || c.ZF;   // le
        case 2: return (c.SF ^ c.OF);           // l
        case 3: return c.ZF;           // e
        case 4: return !c.ZF;          // ne
        case 5: return !(c.SF ^ c.OF); // ge
        case 6: return !(c.SF ^ c.OF) && !c.ZF; // g
        default: return false;
    }
}
```

---

* 严格按照 Y86 条件码语义实现
* 与 RRMOVQ 的条件移动复用

## PC 更新规则

* `JXX`：若条件满足则 `PC = valC`，否则 `PC = valP`
* `CALL`：`PC = valC`
* `RET`：`PC = valM`（从栈上弹出的返回地址）
* `HALT`：`stat = HLT`，PC 不再推进
* 其他指令：`PC = valP`

> 所有控制流在一个 `switch` 内集中处理，便于检查和调试。

---

# JSON 日志与内存压缩输出

## 寄存器与条件码转储

```cpp
json CPU::dump_regs() const {
    json j = json::object();
    for (int i = 0; i < REG_NUM; i++) j[reg_name(i)] = R[i];
    return j;
}

json CPU::dump_cc() const {
    return json{{"OF", cc.OF}, {"SF", cc.SF}, {"ZF", cc.ZF}};
}
```

* 使用 `reg_name(i)` 输出人类可读的寄存器名（rax, rcx, ...）

---

## 压缩内存输出 `dump_mem_nonzero`

* 用 `qword_touched` 记录所有访问过的 8B 对齐地址
* 对每个地址：

  * 调用 `read8` 获取 8 字节值
  * 只把 **非零值** 放入 JSON，对应字段名为地址字符串

> 输出体积小，但仍然足以观察程序行为，适合作为“调试 UI”的数据源。

---

# 设计亮点总结

1. **结构清晰的单步执行函数**

   * 明确划分 Decode / Execute / Memory / Writeback / PC 更新
   * 非常适合教学展示“指令执行过程”

2. **稀疏内存模型 + 边界检查**

   * `std::unordered_map` 实现字节寻址
   * `bounded + mem_upper` 支持实验中的“非法访问检测”

---

3. **面向前端的 JSON 接口**

   * `step()` 直接返回 JSON，方便构建 CLI/TUI/Web 前端
   * 内存压缩后输出，避免日志过多

4. **类型与 ISA 枚举集中**

   * `types.h` 把所有 ISA 常量集中管理
   * 便于扩展、查阅和维护

---

# 不足与改进方向

## 代码结构层面

* `step()` 函数较长，**Decode / Execute / Memory / Writeback** 可以拆成多个子函数或类，增强可维护性
* `CALL/PUSHQ` 在两个阶段写 `rsp`，虽无 bug 但职责不够“纯粹”，可以只在 WB 阶段修改寄存器

---

## 性能与抽象

* 内存读写基于 `unordered_map` + `for` 循环读 8 字节，性能足够实验但大规模程序会较慢

  * 可选优化：改为 `std::vector<u8>` + 懒扩展的“连续内存”，或增加简单 cache
* 每步生成 JSON 也有一定开销，可提供“无日志版本的 step_fast()` 以用于批量执行

---

## 功能扩展

* 当前仅实现顺序执行，后续可以：

  * 显式拆出“流水线寄存器”，支持流水线 / 转发 / 气泡
  * 增加断点、watchpoint、单步多粒度执行等调试功能
* 错误报告较为粗粒度（如 ADR / INS），可以进一步附带“出错地址/指令字节”等更详细信息

---

# 总结

* 本实现通过 `CPU` 抽象 + 单步 `step()` 的方式，较好地复现了 Y86-64 的执行语义
* 设计上兼顾了：

  * 教学上的透明性（每一步状态清晰可见）
  * 实现上的简洁（只依赖 C++ 标准库 + JSON）
* 未来可以在此基础上：

  * 向流水线、性能优化、可视化调试器等方向继续演进
  * 作为理解真实 x86-64 执行模型的“迷你实验平台”

---

# 性能评测脚本 bench_y86.py

- 作用：**Y86-64 simulator** 做“压力测试 + 性能画像”
- 对一批 `.yo` 程序重复运行，统计：
  - 每次运行执行了多少条指令
  - 花了多少时间
  - 平均每秒能执行多少条指令（IPS）
  - 指令与内存访问的大致带宽
  - 动态指令 mix、分支命中情况等
- 所有结果汇总到 `bench_result.json` 中

---

## 测量流程：多次运行 + 汇总

`bench_y86.py` 对每个 `.yo` 文件做了这样的流程：

1. **多次重复运行**
   - 字段 `repeat`：重复次数（基本都是 3）
   - 字段 `runs`：长度为 `repeat` 的数组，每一项是一轮独立测试
2. **每一轮记录：**
   - `time`：本轮总耗时（秒）
   - `N`：本轮执行的指令条数
   - `IPS`：本轮指令吞吐量，满足 `IPS ≈ N / time`
   - `rss`：预留给“常驻内存占用”（这里都是 `null`，说明暂时没用）

---

3. **汇总结果：**
   - `total_N`：所有轮次的 `N` 之和
   - `total_time`：所有轮次的 `time` 之和
   - `IPS_avg`：整体平均性能，满足 `IPS_avg ≈ total_N / total_time`

> 这样可以平滑掉单次测量的随机波动，比只跑一遍更稳定。

---

## JSON 结构示例（以 abs-asum-cmov 为例）

`bench_result.json` 中每个条目（一个 benchmark）大致长这样：

```json
{
  "file": "./test/abs-asum-cmov.yo",
  "runs": [
    {"time": 0.00258, "N": 46, "IPS": 17780.64, "rss": null},
    {"time": 0.00186, "N": 46, "IPS": 24777.26, "rss": null},
    {"time": 0.00191, "N": 46, "IPS": 24074.32, "rss": null}
  ],
  "repeat": 3,
  "total_N": 138,
  "total_time": 0.00635,
  "IPS_avg": 21717.32,
  ...
}
````

---

* 每次运行执行 46 条指令，重复 3 次 → `total_N = 138`

* 总耗时约 0.00635 s → `IPS_avg ≈ 138 / 0.00635 ≈ 21717 条/s`

* 后面的字段是更细的性能画像（见下面几页）

---

## 动态指令 mix：程序“长什么样”

字段 `mix` 是这个 benchmark 在所有轮次里 **实际执行的动态指令统计**：

```json
"mix": {
  "irmovq": 15,
  "call": 6,
  "OPq": 66,
  "jXX": 18,
  "mrmovq": 12,
  "rrmov/cmov": 12,
  "ret": 6,
  "halt": 3
}
```

---

* 这些数量之和 = `total_N`（例如这里 15+6+…+3 = 138）
* 统计的是 **动态执行次数**，不是静态代码行数
* 通过 `mix` 可以看出每个测试程序的“性格”：

  * 计算密集型：`OPq` 占比高
  * 内存密集型：`mrmovq` / `rmmovq` / `pushq` / `popq` 多
  * 调用/返回密集型：`call` / `ret` 多
  * 分支密集型：`jXX` 多

> 这相当于给每个 benchmark 打了一个“指令画像标签”，方便解释 IPS 差异。

---

## 分支行为：branch_taken_ratio

字段 `branch_taken_ratio`：**分支被实际跳转的比例**

* 例子：

  * `abs-asum-cmov.yo`：`jXX = 18`，`branch_taken_ratio ≈ 0.8333`
    → 18 条条件跳转中约有 15 条是“跳了”的
  * `asumr.yo`：`jXX = 15`，`branch_taken_ratio = 0.2`
    → 只有极少数分支会被真正 taken（类似循环尾判断）

---

  * 某些程序该字段为 `0.0` 或 `null`：

    * `0.0`：有分支但都不跳
    * `null`：基本不涉及条件跳转

> 这个指标告诉你：你的 simulator 在“分支大多跳 / 大多不跳 / 基本不跳”的情况下性能表现如何。

---

## 指令 & 内存带宽：ifetch / mem_read / mem_write

对每个 benchmark，还计算了 3 个“带宽”指标：

* `ifetch_MBps`

  * 估计是 **指令取指的总字节数 / 总时间 / 1MB**
  * 根据动态指令 mix 和每条指令长度（2B+可选立即数等）算出总取指字节数
* `mem_read_MBps`

  * 把所有“读内存”的指令（如 `mrmovq`、`popq`、`ret` 等）访问的数据字节累加
  * 再除以总时间，单位换算成 MB/s
* `mem_write_MBps`

  * 类似地对“写内存”（`rmmovq`、`pushq`、`call` 等）统计

---  

例子：`asumr.yo` 的结果：

* `IPS_avg ≈ 33268`
* `ifetch_MBps ≈ 0.167`
* `mem_read_MBps ≈ 0.059`
* `mem_write_MBps ≈ 0.042`

> 这些数字可以帮助你判断：在你的实现中，“取指 / 读 / 写”大概能跑到什么量级。

---

## 测试程序的覆盖面

* `abs-asum-cmov.yo` / `abs-asum-jmp.yo`：

  * 相同功能，用条件传送 vs 条件跳转，对比分支实现开销
* `asum.yo` / `asumi.yo` / `asumr.yo`：

  * 简单数组求和，iterative / recursive 等不同版本，对比调用/栈开销

---

* `cjr.yo`, `j-cc.yo`, `prog1.yo` ~ `prog10.yo`：

  * 各种微型程序，覆盖 `nop`、算术、分支、call/ret 等指令组合
* `pushtest.yo`, `poptest.yo`, `pushquestion.yo`, `ret-hazard.yo`：

  * 强调栈操作和返回相关的行为，观察栈访问的性能表现

---

## 这些指标在“评价性能”时怎么看？

结合前面的字段，你可以这样理解一条记录：

* `IPS_avg`：**核心执行速度**

  * 单位时间内 simulator 能跑多少 Y86 指令
* `mix`：**说明这个 IPS 是在什么负载下测出来的**

  * 算术居多？内存居多？分支/调用多不多？
* `branch_taken_ratio`：**分支模式**

  * 大部分 taken vs 大部分 not-taken → 能看出分支逻辑实现是否有明显偏差

---

* `ifetch_MBps` / `mem_read_MBps` / `mem_write_MBps`：**大致内存吞吐能力**

  * 对比不同程序可以看出：大量 load/store 时，IPS 是否显著下降
* `runs` & `repeat`：**说明结果是多次测出来的平均值**

  * 单次波动不会太影响最终判断

---

## 性能评测设计的亮点与不足

### 亮点

* **多轮测试 + 汇总**：降低单次计时噪声
* **动态指令 mix**：解释“为什么这个程序快/慢”有依据，而不仅仅给出一个 IPS 数字
* **分支 / 内存带宽指标**：

  * 能从“取指 vs 读 vs 写”的角度分析瓶颈大概在哪
* **覆盖多种微型 benchmark**：

  * 算术、内存、分支、栈调用等多维度的负载

---

### 可以扩展/改进的方向

* 增加统计：

  * 每轮的方差 / 标准差，刻画稳定性
  * 更长、更真实一点的程序
* 更细粒度的指标：

  * 如果以后做流水线，可以按“每条指令平均周期”和“停顿/气泡比例”来评价
* 把 `rss` 真正接上 OS 的内存测量（psutil 等），看不同实现的内存占用差异

> 总体来说，这份 bench 脚本已经给你的 simulator 搭好了一个不错的“性能体检面板”，后面优化/改架构时都可以直接复用这套指标来对比前后变化。

---

# 基于 FTXUI 的终端前端

- 使用 **FTXUI** 构建的跨平台 TUI 界面
- 与核心 `CPU`/`worker` 逻辑直接交互，提供“**可视化调试器**”体验  
- 主要能力：
  - 加载并运行 `.yo` 程序
  - 单步 / 连续运行 / 暂停
  - 设置断点并在命中时自动暂停
  - 实时查看寄存器、PC、条件码和内存
  - 查看最近若干步的执行摘要

---

## 程序加载与执行控制

- **加载程序**
  - 顶部输入框中键入 `.yo` 文件路径
  - `Load` / `Reload` 按钮载入程序，并在状态栏显示结果信息
  - 也支持从命令行参数直接传入 `.yo` 文件路径，启动时自动加载

- **执行控制**
  - `Step`：单步执行一条 Y86 指令，并记录快照
  - `Run`：进入自动执行模式（循环 `step_once`）
  - `Stop`：从“运行中”切回“空闲”状态
  - 每一步执行后都会更新界面，方便观察状态变化

---

## 断点与运行时信息展示

- **断点管理**
  - 断点输入框中键入地址（支持十进制或形如 `0x19` 的十六进制）
  - `Add BP`：把该地址加入断点集合
  - `Clear BPs`：清空所有断点
  - 当自动运行过程中 PC 命中某断点时：
    - 自动停止运行
    - 状态栏提示 “Hit breakpoint at PC=...”

---

- **运行时状态展示**
  - PC 当前值、`STAT` 状态（AOK/HLT/ADR/INS）
  - 条件码 CC：`ZF, SF, OF`
  - 断点列表：展示已设置的所有断点地址
  - 底部状态栏还会显示近期操作提示信息（如加载结果、错误提示等）

---

## 寄存器 / 内存 / 执行日志视图

- **寄存器窗口**
  - 左侧面板按行显示 15 个通用寄存器
  - 每行包含寄存器名（如 `rax`）及其当前值（十进制显示）

- **内存视图**
  - 中间面板显示 **非零的 8 字节块**：
    - 按地址从小到大排序
    - 显示基地址 + 该 8 字节解释为有符号整数的值
  - 支持滚动查看更多行（每页约 20 行）

---

- **最近执行日志**
  - 右侧面板展示最近若干条执行记录（最多保留 200 步，其中最近 10 步展示）
  - 每条记录包含：
    - `PC` 值
    - `STAT` 状态
    - 条件码 `ZF,SF,OF`
  - 相当于一个简单的“时间轴”，方便回顾最近的控制流变化

---

## 交互方式与快捷键

- **主要快捷键**（在 TUI 底部也有提示）：
  - `[O]`：加载当前路径 (`Load`)
  - `[S]`：单步执行 (`Step`)
  - `[R]`：连续运行 (`Run`)
  - `[T]`：停止运行 (`Stop`)
  - `[B]`：添加断点 (`Add BP`)
  - `[C]`：清空断点 (`Clear BPs`)
  - `[↑]/[↓]`：滚动内存窗口
  - `[Q]`：退出 TUI

---

- **总结：TUI 能做什么？**
  - 像 IDE 里的调试器一样：
    - 加载 Y86-64 程序
    - 设置断点
    - 单步 / 连续执行
    - 实时观察寄存器、内存与状态变化  
  - 帮助你更直观地理解自己实现的 Y86-64 模拟器的运行过程


---

## Mini-C → Y86-64 编译器

- 自己实现了一个**极简 C 语言子集编译器**：
  - 输入：一个只包含 `int main()` 的 Mini-C 源文件（`.mc`）
  - 输出：对应的 **Y86-64 `.yo` 程序**
- 用途：
  - 把“高级语言”示例快速落到 Y86-64 上
  - 配合前面的 **Y86 模拟器 + FTXUI TUI** 做端到端调试
- 使用方式（示例）：
  - `python3 minic_to_y86.py sum.mc > sum.yo`
  - 或 `./minic_to_y86.py sum.mc > sum.yo`（加可执行权限后）

---

## 支持的 Mini-C 语法子集

> 不追求完整 C，只支持做实验/演示需要的一小块核心语法

- 程序结构
  - **仅支持单函数程序**：`int main() { ... }`
  - 局部变量统一放在 `main` 内声明（函数级作用域）
- 声明与语句
  - 局部变量：`int x;` / `int x = expr;`
  - 赋值语句：`x = expr;`
  - 条件分支：`if (cond) stmt` / `if (cond) stmt else stmt`
  - 循环：`while (cond) stmt`
  - 返回：`return expr;`

---

- 表达式
  - 整数字面量：`123`, `-5`
  - 变量引用：`x`
  - 一元运算：`-expr`
  - 二元运算：
    - 算术：`+`, `-`
    - 按位：`&`, `^`
    - 比较：`==`, `!=`, `<`, `<=`, `>`, `>=`
- **不支持**：
  - 函数调用、数组、指针、结构体等复杂特性
  - 块内重新声明变量（只允许函数开头一段 `int ...;`）

---

## 示例：sum.mc 源程序

> 计算从 1 加到 5 的累加和，最后返回 15

```c
int main() {
    int n = 5;
    int s = 0;
    while (n) {
        s = s + n;
        n = n - 1;
    }
    return s;
}
````

---

## sum.mc 对应的 Y86-64 程序片段（sum.yo）

编译器会生成一个带有启动代码、函数栈帧、局部变量分配和循环控制流的 `.yo` 文件，例如：

```text
0x000: 30f40010000000000000 |   irmovq $4096,%rsp
0x00a: 801400000000000000   |   call main
0x013: 00                   |   halt

...

0x0c5: 30f00000000000000000 |   irmovq $0,%rax
0x0cf: 2054                 |   rrmovq %rbp,%rsp
0x0d1: b05f                 |   popq %rbp
0x0d3: 90                   |   ret
```

---

* 可以看到：

  * 启动代码会设置栈顶，并 `call main`
  * `main` 里有标准的函数序言 / 结尾：

    * 保存 `rbp`、建立新栈帧、为局部变量预留栈空间
    * 函数结束时恢复 `rbp` / `rsp` 并 `ret`
  * `n` 和 `s` 被分配在 `rbp` 附近的栈上（`-8(%rbp)`, `-16(%rbp)`）
  * `while (n) { ... }` 被翻译为一段带条件跳转的循环

---

## 端到端工作流：从 C 到 TUI 调试

整个实验平台最终可以支持这样的一条龙流程：

1. **编写 Mini-C 源码**

   * 如 `sum.mc`，使用受限 C 语法描述算法逻辑
2. **使用编译器生成 Y86-64 程序**

   * `python3 minic_to_y86.py sum.mc > sum.yo`
3. **在 Y86 模拟器中加载 .yo**

   * 命令行或通过 FTXUI TUI 的“加载程序”功能载入 `sum.yo`

---

4. **在 TUI 里调试执行**

   * 设置断点（例如 loop 开始处）
   * 单步 / 连续运行，观察：

     * `PC` 的变化
     * 局部变量 `n`、`s` 的变化（通过寄存器 / 内存视图）
     * 条件码和程序状态（AOK / HLT 等）
5. **验证程序行为**

   * 确认最终返回值是否符合预期（如 `main` 返回 15）
   * 通过状态变化更直观地理解循环和控制流

---

## Mini-C 编译器的定位与局限

* 定位：

  * 面向教学 / 实验的 **玩具级 C 子集编译器**
  * 核心目标是**打通“高级语言 → ISA → 模拟器 → TUI 调试”这一整条链路**
* 当前有意保留的简化：

  * 只支持 `int main()`，没有多函数 / 递归
  * 只支持整型和少量运算符，没有数组 / 指针 / 函数参数
  * 没有优化，生成的是比较“直观”的 Y86 代码
* 带来的好处：

  * 源码规模小，易于完全看懂和修改
  * 每一个 C 语法构造都能在 Y86 上找到清晰对应关系

---

## 总结：从 ISA 到 调试器 的完整实验平台

- **Y86-64 Simulator 核心实现**
  - 用 `CPU` 抽象寄存器 / PC / CC / 稀疏内存
  - `step()` 按取指→译码→执行→访存→写回→PC 更新顺序执行
  - JSON 日志 + 非零内存压缩输出，方便前端展示和调试

- **性能评测 bench_y86.py**
  - 对多组 `.yo` 程序重复运行，统计 `IPS_avg`、动态指令 mix
  - 分析分支行为（branch_taken_ratio）和指令 / 内存带宽
  - 为后续优化模拟器实现（数据结构/算法/架构）提供量化依据

---

- **FTXUI 终端前端（TUI）**
  - 加载 / 重新加载 `.yo` 程序
  - 单步 / 连续运行 / 停止
  - 支持断点，命中自动暂停
  - 实时查看寄存器、PC、条件码、内存和执行历史


- **Mini-C → Y86 编译器**
  - 支持极简 `int main()` + 基本表达式 / 条件 / 循环 / return
  - 将 `.mc` 源码编译为 `.yo` 程序（如 `sum.mc` → `sum.yo`）
  - 和模拟器 + TUI 联动，实现从“C 代码”到“Y86 调试”的端到端链路

> 整体形成：**ISA 定义 → 模拟执行 → 可视化调试 → Mini-C 驱动 → 性能评测** 的完整实验平台，为理解计算机系统各层次提供了一个可动手实验的“小型生态系统”。

---

# 谢谢！

## 2025.11.28

> Powered by Marp