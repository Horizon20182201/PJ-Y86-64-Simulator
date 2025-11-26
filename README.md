# PJ-Y86-64-Simulator

## 简介

一个 Y86-64 指令集模拟器。

## 启动测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TUI=OFF
cmake --build build -j
python3 test.py --bin ./build/y86sim
# 因为首轮文件缓存/页缓存和分配器还没启动，可能导致运行超时，这个时候再执行一次最后一条指令即可。
```

或者

```bash
chmod +x test.sh
./test.sh
```

## 启动性能评测脚本 bench_y86.py

```bash
python3 benchmark/bench_y86.py --sim ./build/y86sim --dir ./test --repeat 3 --json benchmark/bench_result.json
```

脚本运行结果保存在`./benchmark/bench_result.json`

## 启动基于 FTXUI 的终端前端

```bash
cmake -S . -B build_tui -DCMAKE_BUILD_TYPE=Release -DBUILD_TUI=ON
cmake --build build_tui -j
./build_tui/y86_tui test/prog1.yo # 可以换成其它.yo文件
```
## 启动 Mini-C → Y86-64 编译器

```bash
cd minic
chmod +x ./run.sh
./run.sh ./test.mc
```

`.yo`文件保存在`./minic/yo`
