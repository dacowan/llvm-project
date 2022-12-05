
#include "llvm/Transforms/Utils/MojangTransformPassTest.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/Function.h"

#include <map>

#pragma optimize("", off)

using namespace llvm;

// static std::string demangle(const std::string &Mangled) {
//   const char *DecoratedStr = Mangled.c_str();
// 
//   std::string Result;
//   if (nonMicrosoftDemangle(DecoratedStr, Result))
//     return Result;
// 
//   std::string Prefix;
//   char *Undecorated = nullptr;
// 
//   if (!Undecorated && strncmp(DecoratedStr, "__imp_", 6) == 0) {
//     Prefix = "import thunk for ";
//     Undecorated = itaniumDemangle(DecoratedStr + 6, nullptr, nullptr, nullptr);
//   }
// 
//   Result = Undecorated ? Prefix + Undecorated : Mangled;
//   free(Undecorated);
//   return Result;
// }


PreservedAnalyses MojangTransformFunctionTest::run(Function &F,
                                      FunctionAnalysisManager &AM) {
  auto mangledName = F.getName();
  auto demangledName = llvm::demangle(mangledName.str());

  dbgs() << demangledName << "\n";

  return PreservedAnalyses::all();
}


llvm::PreservedAnalyses
MojangTransformCallGraphTest::run(Module &M, ModuleAnalysisManager &AM) {

  auto &CG = AM.getResult<CallGraphAnalysis>(M);

#if false
  unsigned sccNum = 0;
  dbgs() << "SCCs for the program in PostOrder:";
  for (scc_iterator<CallGraph *> SCCI = scc_begin(&CG); !SCCI.isAtEnd();
       ++SCCI) {
    const std::vector<CallGraphNode *> &nextSCC = *SCCI;
    dbgs() << "\nSCC #" << ++sccNum << ": ";
    bool First = true;
    for (std::vector<CallGraphNode *>::const_iterator I = nextSCC.begin(),
                                                      E = nextSCC.end();
         I != E; ++I) {
      if (First)
        First = false;
      else
        dbgs() << ", ";
     auto mangledName = ((*I)->getFunction() ? (*I)->getFunction()->getName()
                                 : "external node");
     auto demangledName = demangle(mangledName.str());
//      if (demangledName.find("__cdecl std::") != std::string::npos) {
//         continue;
//      }

     dbgs() << demangledName;
    }

    if (nextSCC.size() == 1 && SCCI.hasCycle())
      dbgs() << " (Has self-loop).";
  }
  dbgs() << "\n";
#else

  SmallVector<CallGraphNode *, 16> Nodes;

  for (auto fnIt = CG.begin(); fnIt != CG.end(); ++fnIt) {
    Nodes.push_back(fnIt->second.get());
  }

  llvm::sort(Nodes, [](CallGraphNode *LHS, CallGraphNode *RHS) {
    if (Function *LF = LHS->getFunction())
      if (Function *RF = RHS->getFunction())
        return LF->getName() < RF->getName();

    return RHS->getFunction() != nullptr;
  });

  using CallGraphMap = std::unordered_map<CallGraphNode*, std::vector<CallGraphNode*>>;

  auto fnFindNodeThatUsesFunction =
      [Nodes](const std::string name,
              CallGraphMap& graphMap) -> CallGraphNode * {

    for (auto nodeIt = Nodes.rbegin(); nodeIt != Nodes.rend(); ++nodeIt) {
      CallGraphNode *CN = *nodeIt;
      if (!CN->getFunction()) {
        continue;
      }
      // We already mapped this path
      if (graphMap.find(CN) != graphMap.end()) {
        continue;
      }

      for (const auto &I : *CN) {
        if (Function *FI = I.second->getFunction()) {
          std::string mangledName = FI->getName().str();
          if (mangledName.find(name) != std::string::npos) {
            return CN;
          }
        }
      }
    }
    return nullptr;
  };


  std::string rootLookingFor = "getHealth@PlayerProperties";
  CallGraphMap nodeCallGraphs;

  // Find all the invocations of the root function that we're looking for
  // We need all of their call stacks
  auto n = fnFindNodeThatUsesFunction(rootLookingFor, nodeCallGraphs);
  while (n) {
    nodeCallGraphs[n] = {};
    n = fnFindNodeThatUsesFunction(rootLookingFor, nodeCallGraphs);
  }

  // Now go through all the stacked invocations of the function we're looking
  // for, and generate their call stacks
  
  int useCount = 1;
  for (auto& [node, nodeGraph] : nodeCallGraphs) {
    auto lookingFor = rootLookingFor;
    dbgs() << "use #" << useCount << ": " << lookingFor << "\n";
    auto foundNode = node;
    while (foundNode) {
      auto demangledLookingFor = llvm::demangle(lookingFor);
      auto nodeFuncName = foundNode->getFunction()->getName().str();
      auto demangledNodeFuncName = llvm::demangle(nodeFuncName);
      dbgs() << "Looking for `" << lookingFor << "` (" << demangledLookingFor
             << ") - found in `" << nodeFuncName << "` ("
             << demangledNodeFuncName << ")\n ";
      lookingFor = nodeFuncName;
      foundNode = fnFindNodeThatUsesFunction(lookingFor, nodeCallGraphs);
    }
  }


#endif


  return PreservedAnalyses::all();
}
