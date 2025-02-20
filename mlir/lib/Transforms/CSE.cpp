//===- CSE.cpp - Common Sub-expression Elimination ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This transformation pass performs a simple common sub-expression elimination
// algorithm on operations within a region.
//
//===----------------------------------------------------------------------===//

#include "mlir/Transforms/Passes.h"

#include "mlir/IR/Dominance.h"
#include "mlir/Interfaces/SideEffectInterfaces.h"
#include "mlir/Pass/Pass.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/ScopedHashTable.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/RecyclingAllocator.h"
#include <deque>

namespace mlir {
#define GEN_PASS_DEF_CSE
#include "mlir/Transforms/Passes.h.inc"
} // namespace mlir

using namespace mlir;

namespace {
struct SimpleOperationInfo : public llvm::DenseMapInfo<Operation *> {
  static unsigned getHashValue(const Operation *opC) {
    return OperationEquivalence::computeHash(
        const_cast<Operation *>(opC),
        /*hashOperands=*/OperationEquivalence::directHashValue,
        /*hashResults=*/OperationEquivalence::ignoreHashValue,
        OperationEquivalence::IgnoreLocations);
  }
  static bool isEqual(const Operation *lhsC, const Operation *rhsC) {
    auto *lhs = const_cast<Operation *>(lhsC);
    auto *rhs = const_cast<Operation *>(rhsC);
    if (lhs == rhs)
      return true;
    if (lhs == getTombstoneKey() || lhs == getEmptyKey() ||
        rhs == getTombstoneKey() || rhs == getEmptyKey())
      return false;
    return OperationEquivalence::isEquivalentTo(
        const_cast<Operation *>(lhsC), const_cast<Operation *>(rhsC),
        /*mapOperands=*/OperationEquivalence::exactValueMatch,
        /*mapResults=*/OperationEquivalence::ignoreValueEquivalence,
        OperationEquivalence::IgnoreLocations);
  }
};
} // namespace

namespace {
/// Simple common sub-expression elimination.
struct CSE : public impl::CSEBase<CSE> {
  /// Shared implementation of operation elimination and scoped map definitions.
  using AllocatorTy = llvm::RecyclingAllocator<
      llvm::BumpPtrAllocator,
      llvm::ScopedHashTableVal<Operation *, Operation *>>;
  using ScopedMapTy = llvm::ScopedHashTable<Operation *, Operation *,
                                            SimpleOperationInfo, AllocatorTy>;

  /// Cache holding MemoryEffects information between two operations. The first
  /// operation is stored has the key. The second operation is stored inside a
  /// pair in the value. The pair also hold the MemoryEffects between those
  /// two operations. If the MemoryEffects is nullptr then we assume there is
  /// no operation with MemoryEffects::Write between the two operations.
  using MemEffectsCache =
      DenseMap<Operation *, std::pair<Operation *, MemoryEffects::Effect *>>;

  /// Represents a single entry in the depth first traversal of a CFG.
  struct CFGStackNode {
    CFGStackNode(ScopedMapTy &knownValues, DominanceInfoNode *node)
        : scope(knownValues), node(node), childIterator(node->begin()) {}

    /// Scope for the known values.
    ScopedMapTy::ScopeTy scope;

    DominanceInfoNode *node;
    DominanceInfoNode::const_iterator childIterator;

    /// If this node has been fully processed yet or not.
    bool processed = false;
  };

  /// Attempt to eliminate a redundant operation. Returns success if the
  /// operation was marked for removal, failure otherwise.
  LogicalResult simplifyOperation(ScopedMapTy &knownValues, Operation *op,
                                  bool hasSSADominance);
  void simplifyBlock(ScopedMapTy &knownValues, Block *bb, bool hasSSADominance);
  void simplifyRegion(ScopedMapTy &knownValues, Region &region);

  void runOnOperation() override;

private:
  void replaceUsesAndDelete(ScopedMapTy &knownValues, Operation *op,
                            Operation *existing, bool hasSSADominance);

  /// Check if there is side-effecting operations other than the given effect
  /// between the two operations.
  bool hasOtherSideEffectingOpInBetween(Operation *fromOp, Operation *toOp);

  /// Operations marked as dead and to be erased.
  std::vector<Operation *> opsToErase;
  DominanceInfo *domInfo = nullptr;
  MemEffectsCache memEffectsCache;
};
} // namespace

void CSE::replaceUsesAndDelete(ScopedMapTy &knownValues, Operation *op,
                               Operation *existing, bool hasSSADominance) {
  // If we find one then replace all uses of the current operation with the
  // existing one and mark it for deletion. We can only replace an operand in
  // an operation if it has not been visited yet.
  if (hasSSADominance) {
    // If the region has SSA dominance, then we are guaranteed to have not
    // visited any use of the current operation.
    op->replaceAllUsesWith(existing);
    opsToErase.push_back(op);
  } else {
    // When the region does not have SSA dominance, we need to check if we
    // have visited a use before replacing any use.
    for (auto it : llvm::zip(op->getResults(), existing->getResults())) {
      std::get<0>(it).replaceUsesWithIf(
          std::get<1>(it), [&](OpOperand &operand) {
            return !knownValues.count(operand.getOwner());
          });
    }

    // There may be some remaining uses of the operation.
    if (op->use_empty())
      opsToErase.push_back(op);
  }

  // If the existing operation has an unknown location and the current
  // operation doesn't, then set the existing op's location to that of the
  // current op.
  if (existing->getLoc().isa<UnknownLoc>() && !op->getLoc().isa<UnknownLoc>())
    existing->setLoc(op->getLoc());

  ++numCSE;
}

bool CSE::hasOtherSideEffectingOpInBetween(Operation *fromOp, Operation *toOp) {
  assert(fromOp->getBlock() == toOp->getBlock());
  assert(
      isa<MemoryEffectOpInterface>(fromOp) &&
      cast<MemoryEffectOpInterface>(fromOp).hasEffect<MemoryEffects::Read>() &&
      isa<MemoryEffectOpInterface>(toOp) &&
      cast<MemoryEffectOpInterface>(toOp).hasEffect<MemoryEffects::Read>());
  Operation *nextOp = fromOp->getNextNode();
  auto result =
      memEffectsCache.try_emplace(fromOp, std::make_pair(fromOp, nullptr));
  if (result.second) {
    auto memEffectsCachePair = result.first->second;
    if (memEffectsCachePair.second == nullptr) {
      // No MemoryEffects::Write has been detected until the cached operation.
      // Continue looking from the cached operation to toOp.
      nextOp = memEffectsCachePair.first;
    } else {
      // MemoryEffects::Write has been detected before so there is no need to
      // check further.
      return true;
    }
  }
  while (nextOp && nextOp != toOp) {
    auto nextOpMemEffects = dyn_cast<MemoryEffectOpInterface>(nextOp);
    // TODO: Do we need to handle other effects generically?
    // If the operation does not implement the MemoryEffectOpInterface we
    // conservatively assumes it writes.
    if ((nextOpMemEffects &&
         nextOpMemEffects.hasEffect<MemoryEffects::Write>()) ||
        !nextOpMemEffects) {
      result.first->second =
          std::make_pair(nextOp, MemoryEffects::Write::get());
      return true;
    }
    nextOp = nextOp->getNextNode();
  }
  result.first->second = std::make_pair(toOp, nullptr);
  return false;
}

/// Attempt to eliminate a redundant operation.
LogicalResult CSE::simplifyOperation(ScopedMapTy &knownValues, Operation *op,
                                     bool hasSSADominance) {
  // Don't simplify terminator operations.
  if (op->hasTrait<OpTrait::IsTerminator>())
    return failure();

  // If the operation is already trivially dead just add it to the erase list.
  if (isOpTriviallyDead(op)) {
    opsToErase.push_back(op);
    ++numDCE;
    return success();
  }

  // Don't simplify operations with nested blocks. We don't currently model
  // equality comparisons correctly among other things. It is also unclear
  // whether we would want to CSE such operations.
  if (op->getNumRegions() != 0)
    return failure();

  // Some simple use case of operation with memory side-effect are dealt with
  // here. Operations with no side-effect are done after.
  if (!isMemoryEffectFree(op)) {
    auto memEffects = dyn_cast<MemoryEffectOpInterface>(op);
    // TODO: Only basic use case for operations with MemoryEffects::Read can be
    // eleminated now. More work needs to be done for more complicated patterns
    // and other side-effects.
    if (!memEffects || !memEffects.onlyHasEffect<MemoryEffects::Read>())
      return failure();

    // Look for an existing definition for the operation.
    if (auto *existing = knownValues.lookup(op)) {
      if (existing->getBlock() == op->getBlock() &&
          !hasOtherSideEffectingOpInBetween(existing, op)) {
        // The operation that can be deleted has been reach with no
        // side-effecting operations in between the existing operation and
        // this one so we can remove the duplicate.
        replaceUsesAndDelete(knownValues, op, existing, hasSSADominance);
        return success();
      }
    }
    knownValues.insert(op, op);
    return failure();
  }

  // Look for an existing definition for the operation.
  if (auto *existing = knownValues.lookup(op)) {
    replaceUsesAndDelete(knownValues, op, existing, hasSSADominance);
    ++numCSE;
    return success();
  }

  // Otherwise, we add this operation to the known values map.
  knownValues.insert(op, op);
  return failure();
}

void CSE::simplifyBlock(ScopedMapTy &knownValues, Block *bb,
                        bool hasSSADominance) {
  for (auto &op : *bb) {
    // If the operation is simplified, we don't process any held regions.
    if (succeeded(simplifyOperation(knownValues, &op, hasSSADominance)))
      continue;

    // Most operations don't have regions, so fast path that case.
    if (op.getNumRegions() == 0)
      continue;

    // If this operation is isolated above, we can't process nested regions with
    // the given 'knownValues' map. This would cause the insertion of implicit
    // captures in explicit capture only regions.
    if (op.mightHaveTrait<OpTrait::IsIsolatedFromAbove>()) {
      ScopedMapTy nestedKnownValues;
      for (auto &region : op.getRegions())
        simplifyRegion(nestedKnownValues, region);
      continue;
    }

    // Otherwise, process nested regions normally.
    for (auto &region : op.getRegions())
      simplifyRegion(knownValues, region);
  }
  // Clear the MemoryEffects cache since its usage is by block only.
  memEffectsCache.clear();
}

void CSE::simplifyRegion(ScopedMapTy &knownValues, Region &region) {
  // If the region is empty there is nothing to do.
  if (region.empty())
    return;

  bool hasSSADominance = domInfo->hasSSADominance(&region);

  // If the region only contains one block, then simplify it directly.
  if (region.hasOneBlock()) {
    ScopedMapTy::ScopeTy scope(knownValues);
    simplifyBlock(knownValues, &region.front(), hasSSADominance);
    return;
  }

  // If the region does not have dominanceInfo, then skip it.
  // TODO: Regions without SSA dominance should define a different
  // traversal order which is appropriate and can be used here.
  if (!hasSSADominance)
    return;

  // Note, deque is being used here because there was significant performance
  // gains over vector when the container becomes very large due to the
  // specific access patterns. If/when these performance issues are no
  // longer a problem we can change this to vector. For more information see
  // the llvm mailing list discussion on this:
  // http://lists.llvm.org/pipermail/llvm-commits/Week-of-Mon-20120116/135228.html
  std::deque<std::unique_ptr<CFGStackNode>> stack;

  // Process the nodes of the dom tree for this region.
  stack.emplace_back(std::make_unique<CFGStackNode>(
      knownValues, domInfo->getRootNode(&region)));

  while (!stack.empty()) {
    auto &currentNode = stack.back();

    // Check to see if we need to process this node.
    if (!currentNode->processed) {
      currentNode->processed = true;
      simplifyBlock(knownValues, currentNode->node->getBlock(),
                    hasSSADominance);
    }

    // Otherwise, check to see if we need to process a child node.
    if (currentNode->childIterator != currentNode->node->end()) {
      auto *childNode = *(currentNode->childIterator++);
      stack.emplace_back(
          std::make_unique<CFGStackNode>(knownValues, childNode));
    } else {
      // Finally, if the node and all of its children have been processed
      // then we delete the node.
      stack.pop_back();
    }
  }
}

void CSE::runOnOperation() {
  /// A scoped hash table of defining operations within a region.
  ScopedMapTy knownValues;

  domInfo = &getAnalysis<DominanceInfo>();
  Operation *rootOp = getOperation();

  for (auto &region : rootOp->getRegions())
    simplifyRegion(knownValues, region);

  // If no operations were erased, then we mark all analyses as preserved.
  if (opsToErase.empty())
    return markAllAnalysesPreserved();

  /// Erase any operations that were marked as dead during simplification.
  for (auto *op : opsToErase)
    op->erase();
  opsToErase.clear();

  // We currently don't remove region operations, so mark dominance as
  // preserved.
  markAnalysesPreserved<DominanceInfo, PostDominanceInfo>();
  domInfo = nullptr;
}

std::unique_ptr<Pass> mlir::createCSEPass() { return std::make_unique<CSE>(); }
