#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
bench_y86.py  —  Y86-64 软件模拟器基准脚本
功能：
  * 读取 .yo，调用 y86sim（stdin<- .yo，stdout-> JSON 日志）
  * 解析日志得到动态指令数与 PC 轨迹，解码每条指令，统计指标
  * 打印汇总；可 --json 输出到文件
跨平台：Linux/macOS/Windows（Windows 上内存占用统计将自动降级）
"""

import argparse, json, os, re, subprocess, sys, time
from collections import Counter, defaultdict

try:
    import psutil  # optional
except Exception:
    psutil = None

# ---------------- yo parser -> memory bytes ----------------
HEX = re.compile(r'0x([0-9a-fA-F]+):\s*([0-9a-fA-F]+)')

def parse_yo(path):
    """return (mem: dict[addr->byte], entry:min_addr)"""
    mem = {}
    minaddr = None
    with open(path, 'r', encoding='utf-8') as f:
        for line in f:
            m = HEX.search(line)
            if not m: continue
            addr = int(m.group(1), 16)
            hexs = m.group(2)
            if minaddr is None or addr < minaddr: minaddr = addr
            for i in range(0, len(hexs), 2):
                b = int(hexs[i:i+2], 16)
                mem[addr + (i//2)] = b
    if minaddr is None: minaddr = 0
    return mem, minaddr

# ---------------- Y86 decoder (length, type, valP) ----------
def decode_at(mem, pc):
    """return dict(icode, ifun, len, valP). mem is dict addr->byte"""
    b0 = mem.get(pc, 0)
    icode = (b0 >> 4) & 0xF
    ifun  = b0 & 0xF
    L = 1
    if icode in (0x0,0x1,0x9):    # halt,nop,ret
        L = 1
    elif icode in (0x2,0x6,0xA,0xB):  # rrmov/opq/push/pop
        L = 2
    elif icode in (0x3,0x4,0x5):  # irmov/rmmov/mrmov
        L = 10
    elif icode in (0x7,0x8):      # jXX/call
        L = 9
    else:
        # unknown — treat as 1 to keep going
        L = 1
    return {"icode":icode, "ifun":ifun, "len":L, "valP": pc + L}

# classify helpers
ICODE_NAME = {
    0x0:"halt", 0x1:"nop", 0x2:"rrmov/cmov", 0x3:"irmovq",
    0x4:"rmmovq", 0x5:"mrmovq", 0x6:"OPq", 0x7:"jXX",
    0x8:"call",  0x9:"ret",    0xA:"pushq", 0xB:"popq"
}

def mem_bytes_rw(icode):
    rd = 0; wr = 0
    if icode in (0x5, 0x9, 0xB): rd = 8      # mrmovq, ret, popq
    if icode in (0x4, 0x8, 0xA): wr = 8      # rmmovq, call, pushq
    return rd, wr

# -------------- run simulator ------------------------
def run_sim(sim_bin, yo_path, warmup=False):
    """return (wall_time, json_logs, rss_peak_bytes[optional])"""
    # Read yo
    with open(yo_path,'r',encoding='utf-8') as f:
        yo_txt = f.read()

    # Prepare process
    if psutil:
        proc = psutil.Popen([sim_bin], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    else:
        proc = subprocess.Popen([sim_bin], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)

    t0 = time.perf_counter()
    out, err = proc.communicate(input=yo_txt)
    t1 = time.perf_counter()
    wall = t1 - t0

    rss_peak = None
    if psutil:
        try:
            info = proc.memory_info()
            rss_peak = info.rss
        except Exception:
            rss_peak = None

    try:
        logs = json.loads(out) if out.strip() else []
    except Exception as e:
        raise RuntimeError(f"Simulator output is not valid JSON. stderr:\n{err}\n") from e

    return wall, logs, rss_peak

# --------------- metrics from logs + yo ----------------
def analyze(yo_path, sim_bin, repeat=1):
    mem, entry = parse_yo(yo_path)

    sums = {
        "N":0, "time":0.0, "ifetch_bytes":0, "mem_read":0, "mem_write":0,
        "jxx_total":0, "jxx_taken":0
    }
    mix = Counter()  # by icode name
    per_run = []

    for r in range(repeat):
        wall, logs, rss = run_sim(sim_bin, yo_path)
        N = len(logs)
        if N == 0:  # empty / early error
            per_run.append({"time":wall,"N":N,"IPS":0,"rss":rss})
            continue

        # dynamic trace: start pc is entry, then each step's "post-PC"
        pc_prev = entry
        ifetch_bytes = 0; mem_r = 0; mem_w = 0
        jxx_total = 0; jxx_taken = 0
        kind = Counter()

        for i in range(N):
            d = decode_at(mem, pc_prev)
            nm = ICODE_NAME.get(d["icode"], f"unk_{d['icode']}")
            kind[nm] += 1
            ifetch_bytes += d["len"]
            rd, wr = mem_bytes_rw(d["icode"])
            mem_r += rd; mem_w += wr
            # taken branch?
            post_pc = int(logs[i]["PC"])
            if d["icode"] == 0x7:  # jXX
                jxx_total += 1
                if post_pc != d["valP"]:
                    jxx_taken += 1
            pc_prev = post_pc

        sums["N"] += N
        sums["time"] += wall
        sums["ifetch_bytes"] += ifetch_bytes
        sums["mem_read"] += mem_r
        sums["mem_write"] += mem_w
        sums["jxx_total"] += jxx_total
        sums["jxx_taken"] += jxx_taken
        mix.update(kind)

        per_run.append({
            "time": wall,
            "N": N,
            "IPS": N/wall if wall>0 else float('inf'),
            "rss": rss
        })

    # aggregate
    agg = {
        "file": yo_path,
        "runs": per_run,
        "repeat": repeat,
        "total_N": sums["N"],
        "total_time": sums["time"],
        "IPS_avg": (sums["N"]/sums["time"]) if sums["time"]>0 else float('inf'),
        "ifetch_MBps": (sums["ifetch_bytes"]/1e6)/sums["time"] if sums["time"]>0 else 0.0,
        "mem_read_MBps": (sums["mem_read"]/1e6)/sums["time"] if sums["time"]>0 else 0.0,
        "mem_write_MBps": (sums["mem_write"]/1e6)/sums["time"] if sums["time"]>0 else 0.0,
        "branch_taken_ratio": (sums["jxx_taken"]/sums["jxx_total"]) if sums["jxx_total"]>0 else None,
        "mix": mix
    }
    return agg

def human_report(agg):
    print(f"== {os.path.basename(agg['file'])} ==")
    print(f" repeat: {agg['repeat']}  total_N: {agg['total_N']}  total_time: {agg['total_time']:.6f}s  IPS_avg: {agg['IPS_avg']:.2f}")
    print(f" IFetch: {agg['ifetch_MBps']:.2f} MB/s   DataR: {agg['mem_read_MBps']:.2f} MB/s   DataW: {agg['mem_write_MBps']:.2f} MB/s")
    if agg['branch_taken_ratio'] is not None:
        print(f" jXX taken ratio: {agg['branch_taken_ratio']*100:.1f}%")
    # per-run
    for i, r in enumerate(agg['runs']):
        rss = f"  rss={r['rss']/1e6:.1f}MB" if r['rss'] else ""
        print(f"  run#{i+1}: time={r['time']:.6f}s  N={r['N']}  IPS={r['IPS']:.2f}{rss}")
    # mix
    print(" mix:", ', '.join(f"{k}:{v}" for k,v in agg['mix'].most_common()))
    print()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sim', default='./build/y86sim', help='y86sim executable')
    ap.add_argument('--yo', nargs='+', help='.yo files; or use --dir')
    ap.add_argument('--dir', help='directory containing .yo')
    ap.add_argument('--repeat', type=int, default=3)
    ap.add_argument('--json', help='write aggregated JSON here')
    args = ap.parse_args()

    files = []
    if args.yo: files = args.yo
    if args.dir:
        for n in os.listdir(args.dir):
            if n.endswith('.yo'): files.append(os.path.join(args.dir,n))
    if not files:
        print("No .yo files. Use --yo a.yo b.yo or --dir test", file=sys.stderr)
        sys.exit(1)

    allres = []
    for fp in sorted(files):
        agg = analyze(fp, args.sim, repeat=args.repeat)
        allres.append(agg)
        human_report(agg)

    if args.json:
        # JSON-friendly (Counter -> dict)
        def norm(x):
            j = dict(x)
            j['mix'] = dict(j['mix'])
            return j
        with open(args.json,'w',encoding='utf-8') as f:
            json.dump([norm(a) for a in allres], f, ensure_ascii=False, indent=2)

if __name__=='__main__':
    main()
