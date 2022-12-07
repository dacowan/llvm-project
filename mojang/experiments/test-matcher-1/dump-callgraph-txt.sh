../../../build/RelWithDebInfo/bin/clang -S -emit-llvm -O0 main.cpp -o main.ll
../../../build/RelWithDebInfo/bin/opt.exe -passes='print-callgraph' main.ll -o main.ll.dot 2>out.txt
