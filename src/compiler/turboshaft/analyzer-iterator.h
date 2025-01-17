// Copyright 2023 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_TURBOSHAFT_ANALYZER_ITERATOR_H_
#define V8_COMPILER_TURBOSHAFT_ANALYZER_ITERATOR_H_

#include "src/base/logging.h"
#include "src/compiler/turboshaft/graph.h"
#include "src/compiler/turboshaft/index.h"
#include "src/compiler/turboshaft/loop-finder.h"
#include "src/compiler/turboshaft/operations.h"
#include "src/compiler/turboshaft/sidetable.h"

namespace v8::internal::compiler::turboshaft {

// AnalyzerIterator provides methods to iterate forward a Graph in a way that is
// efficient for the SnapshotTable: blocks that are close in the graphs will be
// visited somewhat consecutively (which means that the SnapshotTable shouldn't
// have to travel far).
//
// To understand why this is important, consider the following graph:
//
//                          B1 <------
//                          |\       |
//                          | \      |
//                          |  v     |
//                          |   B27---
//                          v
//                          B2 <------
//                          |\       |
//                          | \      |
//                          |  v     |
//                          |   B26---
//                          v
//                          B3 <------
//                          |\       |
//                          | \      |
//                          |  v     |
//                          |   B25---
//                          v
//                         ...
//
// If we iterate its blocks in increasing ID order, then we'll visit B1, B2,
// B3... and only afterwards will we visit the Backedges. If said backedges can
// update the loop headers snapshots, then when visiting B25, we'll decide to
// revisit starting from B3, and will revisit everything after, then same thing
// for B26 after which we'll start over from B2 (and thus even revisit B3 and
// B25), etc, leading to a quadratic (in the number of blocks) analysis.
//
// Instead, the visitation order offered by AnalyzerIterator is a BFS in the
// dominator tree (ie, after visiting a node, AnalyzerIterator visit the nodes
// it dominates), with an subtlety for loops: when a node dominates multiple
// nodes, successors that are in the same loop as the current node are visited
// before nodes that are in outer loops.
// In the example above, the visitation order would thus be B1, B27, B2, B26,
// B3, B25.
//
// The MarkLoopForRevisit method can be used when visiting a backedge to
// instruct AnalyzerIterator that the loop to which this backedge belongs should
// be revisited. All of the blocks of this loop will then be revisited.
//
// Implementation details for revisitation of loops:
//
// In order to avoid visiting loop exits (= blocks whose dominator is in a loop
// but which aren't themselves in the loop) multiple times, the stack of Blocks
// to visit contains pairs of "block, generation", where "generation" is a
// counter that is initially 1 and is incremented when revisiting loops.
// Example: The first time a loop header is visited, say with a generation "n",
// we mark in {visited_} that it has been visited with generation "n", and add
// its sucessors to the {stack_} with generation "n" as well. When we decide to
// revisit the loop, we'll add the loop header with generation "n+1" to the
// stack, visit it on the next call to "Next", and again add its children with
// generation "n+1" on the stack. When we encounter on the stack a node whose
// generation is "n" but {visited_} says that this node has already been visited
// with generation "m" with "m>=n", we skip this stack entry.

class AnalyzerIterator {
 public:
  AnalyzerIterator(Zone* phase_zone, Graph& graph,
                   const LoopFinder& loop_finder)
      : graph_(graph),
        loop_finder_(loop_finder),
        visited_(graph.block_count(), kNotVisitedGeneration, phase_zone),
        stack_(phase_zone) {
    stack_.push_back({&graph.StartBlock(), kGenerationForFirstVisit});
  }

  bool HasNext() const {
    DCHECK_IMPLIES(!stack_.empty(), !IsOutdated(stack_.back()));
    return !stack_.empty();
  }
  Block* Next();
  void MarkLoopForRevisit();

 private:
  struct StackNode {
    Block* block;
    uint64_t generation;
  };
  static constexpr uint64_t kNotVisitedGeneration = 0;
  static constexpr uint64_t kGenerationForFirstVisit = 1;

  void PopOutdated();
  bool IsOutdated(StackNode node) const {
    return visited_[node.block->index()] >= node.generation;
  }

  Graph& graph_;
  const LoopFinder& loop_finder_;

  // The last block returned by Next.
  StackNode curr_ = {nullptr, 0};

  // {visited_} maps BlockIndex to the generation they were visited with. If a
  // Block has been visited with a generation `n`, then we never want to revisit
  // it with a generation `k` when `k <= n`.
  FixedBlockSidetable<uint64_t> visited_;

  // The stack of blocks that are left to visit. We maintain the invariant that
  // the .back() of {stack_} is never out-dated (ie, its generation is always
  // greater than the generation for its node recorded in {visited_}), so that
  // "Next" can simply check whether {stack_} is empty or not.
  ZoneVector<StackNode> stack_;
};

}  // namespace v8::internal::compiler::turboshaft

#endif  // V8_COMPILER_TURBOSHAFT_ANALYZER_ITERATOR_H_
