# python test.py --bin ./cpu
# python test.py --bin "python cpu.py"
# or customize your testing command

# optional
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILED_TUI=OFF
cmake --build build -j

# testing command
python3 test.py --bin ./build//y86sim