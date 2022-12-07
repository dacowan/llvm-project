
#include "llvm/Transforms/Utils/MojangTransformPassTest.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/Analysis/CallGraph.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/Function.h"

#include <map>
#include <regex>

#pragma optimize("", off)

using namespace llvm;

// ----------------------------------------------------------------

PreservedAnalyses
MojangTransformFunctionTest::run(Function &F, FunctionAnalysisManager &AM) {
  auto mangledName = F.getName();
  auto demangledName = llvm::demangle(mangledName.str());

  dbgs() << demangledName << "\n";

  return PreservedAnalyses::all();
}

// ----------------------------------------------------------------

std::string extractFunctionName(const std::string &name) {
  std::regex re(
      R"((?:[a-z|A-Z|_][a-z|A-Z|_|0-9]*::)*[a-z|A-Z|_][a-z|A-Z|_|0-9]+\(.*\)$)");
  std::smatch result;
  if (std::regex_search(name, result, re)) {
    if (result.size()) {
      return result[0].str();
    }
  }
  return name;
}


class MojangCGNode {
public:
  MojangCGNode(llvm::CallGraphNode *node) 
      : mNode(node) {

    mMangledName = node->getFunction()->getName().str();
    mDemangledName = llvm::demangle(mMangledName);
  }

  std::vector<std::pair<bool, MojangCGNode *>> &getCallers() {
    return mCallers;
  }
  std::vector<MojangCGNode *> &getCallees() { return mCallees; }

  llvm::CallGraphNode *getCGNode() const { return mNode; }

  void markCallerAsVisited(MojangCGNode *n) {
    for (auto &[visited, node] : mCallers) {
      if (node == n) {
        visited = true;
        break;
      }
    }
  }

  void addCaller(MojangCGNode *value) {
    const auto &&it = std::find_if(mCallers.begin(), mCallers.end(), 
        [value](const std::pair<bool, MojangCGNode*>& nodepair) {
      return nodepair.second == value;
      }
    );
    if (it == mCallers.end()) {
      mCallers.push_back(std::make_pair(false, value));
    }
  }

  void addCallee(MojangCGNode *value) { 
    const auto &&it = std::find(mCallees.begin(), mCallees.end(), value);
    if (it == mCallees.end()) {
      mCallees.push_back(value);
    }
    value->addCaller(this);
  }

  const std::string &getName() const { return mMangledName; }
  const std::string &getDemangledName() const { return mDemangledName; }

protected:
  std::vector<std::pair<bool, MojangCGNode *>> mCallers; // (bool = visited, MojangCGNode*)
  std::vector<MojangCGNode*> mCallees; 
  llvm::CallGraphNode *mNode;

  std::string mMangledName;
  std::string mDemangledName;
};



llvm::PreservedAnalyses
MojangTransformCallGraphTest::run(Module &M, ModuleAnalysisManager &AM) {

  auto &CG = AM.getResult<CallGraphAnalysis>(M);

  std::vector<std::unique_ptr<MojangCGNode>> cgNodeList;
  std::unordered_map<CallGraphNode *, MojangCGNode *> callgraphMap;

  auto _findMojangNodeByName = [&callgraphMap](const std::string name) -> MojangCGNode* {
    for (auto [cgNode, mjNode] : callgraphMap) {
      auto func = cgNode->getFunction();
      if (func && func->getName().str().find(name) != std::string::npos) {
        return mjNode;
      }
    }
    assert(false);
    return nullptr;
  };


  auto _renderGraph = 
      [&out = dbgs()](const std::vector<MojangCGNode *> &graph) {
    out << "\n";
    auto &&it = graph.begin();
    while (it != graph.end()) {
      const auto &node = *it;
      std::string prettyName = extractFunctionName(node->getDemangledName());
      out << prettyName;
      ++it;
      if (it != graph.end()) {
        out << " --> ";
      }
    }
    out << "\n";
  };


  // populate the mojang class list and mappings first -- strip out the ones that aren't associated
  // with functions (not sure what they are for, but they confuse things/me)
    for (auto fnIt = CG.begin(); fnIt != CG.end(); ++fnIt) {
    auto cgNode = fnIt->second.get();
      if (!cgNode->getFunction()) {
        continue;
      }
      auto mjNode = std::make_unique<MojangCGNode>(cgNode);
      callgraphMap[cgNode] = mjNode.get();
      cgNodeList.push_back(std::move(mjNode));
    }


  // Now iterate over all of the nodes and hook them up to each other so
    // we have an actual doubly linked list of graph nodes
    for (auto [cgNode, mojangNode] : callgraphMap) {

      auto name = cgNode->getFunction()->getName().str();

      // Iterate across all the functions in the node
      for (const auto &fn : *cgNode) {
        if (Function *functionDef = fn.second->getFunction()) {
          std::string mangledName = functionDef->getName().str();
          if (auto functionDefNode = _findMojangNodeByName(mangledName)) {
            mojangNode->addCallee(functionDefNode);
          } else {
            assert(false);
          }
        }
      }
    }

    // All of the nodes in cgNodeList should all have ptr's back and forth to the callers
    // and callees... so now it should be possible to generate a bottom up call stack for
    // any functions we're interested in.
    // Remember that a single function can have many paths back to the root though...

    { 
      std::string interestingFunction = "?I@@YAXXZ";

      std::vector<std::vector<MojangCGNode *>> graphList;

      std::function<void(MojangCGNode*)> fnVisitTree = [&graphList, &fnVisitTree](MojangCGNode* node) {

          std::vector<MojangCGNode *> &graph = graphList.back();
          graph.push_back(node);
          int graphDepthSoFar = graph.size();

          const auto &callers = node->getCallers();
          auto &&itCallers = callers.begin();

          while (itCallers != callers.end()) {
            const auto& callerPair = *itCallers;
            fnVisitTree(callerPair.second);

             ++itCallers;
             if (itCallers != callers.end()) {
               graphList.push_back({});
               std::vector<MojangCGNode *> &oldGraph = graphList[graphList.size()-2];
               std::vector<MojangCGNode *> &newGraph = graphList[graphList.size()-1];
               std::copy(oldGraph.begin(), oldGraph.begin() + graphDepthSoFar,
                         std::back_inserter(newGraph));
             }
          }
    
      };

      auto rootNode = _findMojangNodeByName(interestingFunction);
      if (rootNode) {
        graphList.push_back({});
        fnVisitTree(rootNode);

        // render the graphs
        for (auto&& graph : graphList) {
          _renderGraph(graph);
        }
      }


    }

  return PreservedAnalyses::all();
}

#if false

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

  // Not sure why we need to sort function names by alpha?  What's the point?
//   llvm::sort(Nodes, [](CallGraphNode *LHS, CallGraphNode *RHS) {
//     if (Function *LF = LHS->getFunction())
//       if (Function *RF = RHS->getFunction())
//         return LF->getName() < RF->getName();
// 
//     return RHS->getFunction() != nullptr;
//   });

  using CallGraphList = std::vector<CallGraphNode*>;
  using CallGraphMap = std::unordered_map<CallGraphNode*, CallGraphList>;

  CallGraphMap nodeMap;

  std::string functionToLookFor = "getHealth@PlayerProperties";

  // Iterate across all the nodes, and find the function name we're interested in
  // Make a list of these nodes - these are the end points of our graphs
  for (auto nodeIt = Nodes.rbegin(); nodeIt != Nodes.rend(); ++nodeIt) {
    CallGraphNode* node = *nodeIt;
    if (!node->getFunction()) {
      continue;
    }

    // Iterate across all the function calls within this node to see if one of them
    // is the function call we're looking for
    for (const auto &I : *node) {
      if (Function *FI = I.second->getFunction()) {
        std::string mangledName = FI->getName().str();
        if (mangledName.find(functionToLookFor) != std::string::npos) {
          // Found an invocation of our function of interest - push it into the map
          nodeMap[node] = {};
          break;
        }
      }
    }
  }

  // Now we have a map full of keys, comprised of the nodes which invoke our function
  // We now have to traverse the 


  auto fnFindNodeThatUsesFunction =
      [Nodes](const std::string name,
              CallGraphMap graphMap) -> CallGraphNode * {

    for (auto nodeIt = Nodes.rbegin(); nodeIt != Nodes.rend(); ++nodeIt) {
      CallGraphNode *CN = *nodeIt;
      if (!CN->getFunction()) {
        continue;
      }
      // We already mapped this path
//       if (graphMap.find(CN) != graphMap.end()) {
//         continue;
//       }

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
    dbgs() << "\nuse #" << useCount++ << ": " << lookingFor << "\n";

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

#endif