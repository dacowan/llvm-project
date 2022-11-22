cmake -S llvm -B build -DLLVM_ENABLE_PROJECTS="clang;clang-tools-extra;" -DLLVM_TARGETS_TO_BUILD=X86 -Thost=x64
