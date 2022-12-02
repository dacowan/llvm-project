../../../build/RelWithDebInfo/bin/clang -S -emit-llvm -O0 main.cpp -o main.ll
../../../build/RelWithDebInfo/bin/opt.exe -passes='dot-callgraph' main.ll -o main.ll.dot
/c/Program\ Files/Graphviz/bin/dot.exe -Tsvg main.ll.callgraph.dot -o main.ll.svg