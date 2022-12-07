
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
    // A simple regex to extract the class/function name and parameters from a fully
    // qualified decorated or undecorated C++ compiler name
    // e.g. "?B@@YAXXZ" -> "void __cdecl B(void)" --> "B(void)"
    // or "?testA@Test@@QEAAXXZ" -> ""public: void __cdecl Test::testA(void)"" --> "Test::testA(void)"
  std::string undecorated = llvm::demangle(name);
  std::regex re(
      R"((?:[a-z|A-Z|_][a-z|A-Z|_|0-9]*::)*[a-z|A-Z|_|0-9]+\([a-z|A-Z|_|0=9]*\)$)");
  std::smatch result;
  if (std::regex_search(undecorated, result, re)) {
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
    mSimpleName = extractFunctionName(mMangledName);
  }

  std::vector<MojangCGNode *> &getCallers() { return mCallers; }
  std::vector<MojangCGNode *> &getCallees() { return mCallees; }

  llvm::CallGraphNode *getCGNode() const { return mNode; }

  void addCaller(MojangCGNode *value) {
    const auto &&it = std::find(mCallers.begin(), mCallers.end(), value);
    if (it == mCallers.end()) {
      mCallers.push_back(value);
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
  const std::string &getSimpleName() const { return mSimpleName; }

protected:
  std::vector<MojangCGNode *> mCallers;
  std::vector<MojangCGNode*> mCallees; 
  llvm::CallGraphNode *mNode;

  std::string mMangledName;
  std::string mDemangledName;
  std::string mSimpleName;
};



llvm::PreservedAnalyses
MojangTransformCallGraphTest::run(Module &M, ModuleAnalysisManager &AM) {

  auto &CG = AM.getResult<CallGraphAnalysis>(M);

  std::vector<std::unique_ptr<MojangCGNode>> cgNodeList;
  std::unordered_map<CallGraphNode *, MojangCGNode *> callgraphMap;

  // Utility function to find a node via it's full name
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

  // Just render a list of node names
  auto _renderGraph = 
      [&out = dbgs()](const std::vector<MojangCGNode *> &graph) {
    out << "\n";
    auto &&it = graph.begin();
    while (it != graph.end()) {
      const auto &node = *it;
      out << node->getSimpleName();
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

      // Iterate across all the functions in the node -- add the callee functions to
      // the Mojang node -- this will also populate the callees caller function, so as we
      // traverse down the list of nodes, we'll eventually cross pollinate the nodes so that
      // they all point to each other.

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
    // Remember that a single function can have many paths back to the root though... so a
    // single function at the bottom of the graph may resolve to many paths back to a root
    // (or multiple roots).  In addition, some of these paths may also have shared parts.
    // The easiest way to parse all of these paths is a bottom up recursive traveler -
    // but each split point needs to duplicate the path that it took up to that point, so
    // that the final result has a full path from bottom to top (it's easier than trying to
    // maintain lists of partial paths and stitch them together)

    { 
      std::string interestingFunction = "?I@@YAXXZ";

      std::vector<std::vector<MojangCGNode *>> graphList;

      std::function<void(MojangCGNode*)> fnVisitTree = [&graphList, &fnVisitTree](MojangCGNode* node) {

          // get the current path that we're working on, and push the current node
          std::vector<MojangCGNode *> &graph = graphList.back();
          graph.push_back(node);

          // record the size of the graph to get to this point
          int graphDepthSoFar = graph.size();

          const auto &callers = node->getCallers();
          auto &&itCallers = callers.begin();

          while (itCallers != callers.end()) {
            const auto caller = *itCallers;

            // first iteration over this loop will just continue along the current graph
            // path that was on function entry, and will keep going until it gets to the root
            // or dead end.
            // subsequent iterations, however, will create a new route (duplicate the path that
            // has been taken this far into the new route) and continue recursively tracking until
            // it gets to the root or dead end.
            // This will keep happening at each route split point in the overall graph tree until
            // all the routes are solved
            fnVisitTree(caller);

             ++itCallers;
             if (itCallers != callers.end()) {
               // Push a new route and dupe the route that got us here - further iterations will 
               // now build on this route as it traverses the new route to the root
               graphList.push_back({});
               std::vector<MojangCGNode *> &oldGraph = graphList[graphList.size()-2];
               std::vector<MojangCGNode *> &newGraph = graphList[graphList.size()-1];
               std::copy(oldGraph.begin(), oldGraph.begin() + graphDepthSoFar,
                         std::back_inserter(newGraph));
             }
          }
    
      };

      // Solve all of the routes for the interesting function name, and make a list of node lists
      // which represent the callers
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

