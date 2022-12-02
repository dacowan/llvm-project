#ifndef LLVM_TRANSFORMS_UTILS_MOJANG_TRANSFORM_PASS_TEST_H
#define LLVM_TRANSFORMS_UTILS_MOJANG_TRANSFORM_PASS_TEST_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class MojangTransformFunctionTest : public PassInfoMixin<MojangTransformFunctionTest> {
public:
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  static bool isRequired() { return true; }
};

class MojangTransformCallGraphTest
    : public PassInfoMixin<MojangTransformCallGraphTest> {
public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_MOJANG_TRANSFORM_PASS_TEST_H
