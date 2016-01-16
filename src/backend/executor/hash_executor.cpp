//===----------------------------------------------------------------------===//
//
//                         PelotonDB
//
// hash_set_op_executor.cpp
//
// Identification: src/backend/executor/hash_set_op_executor.cpp
//
// Copyright (c) 2015, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <utility>
#include <vector>

#include "backend/common/logger.h"
#include "backend/common/value.h"
#include "backend/executor/logical_tile.h"
#include "backend/executor/hash_executor.h"
#include "backend/planner/hash_plan.h"
#include "backend/expression/tuple_value_expression.h"

namespace peloton {
namespace executor {

/**
 * @brief Constructor
 */
HashExecutor::HashExecutor(const planner::AbstractPlan *node,
                           ExecutorContext *executor_context)
: AbstractExecutor(node, executor_context) {}

/**
 * @brief Do some basic checks and initialize executor state.
 * @return true on success, false otherwise.
 */
bool HashExecutor::DInit() {
  assert(children_.size() == 1);

  // Initialize executor state
  done_ = false;
  result_itr = 0;

  return true;
}

bool HashExecutor::DExecute() {
  LOG_INFO("Hash Executor");

  if (!done_) {
    const planner::HashPlan &node = GetPlanNode<planner::HashPlan>();

    // First, get all the input logical tiles
    //AbstractExecutor* pchild = children_[0];
    //std::cout << pchild;
    while (children_[0]->Execute()) {
      child_tiles_.emplace_back(children_[0]->GetOutput());
    }

    if (child_tiles_.size() == 0)
      return false;

    /* *
     * HashKeys is a vector of TupleValue expr
     * from which we construct a vector of column ids that represent the
     * attributes of the underlying table.
     * The hash table is built on top of these hash key attributes
     * */
    auto &hashkeys = node.GetHashKeys();

    // Construct a logical tile
    for (auto &hashkey : hashkeys) {
      assert(hashkey->GetExpressionType() == EXPRESSION_TYPE_VALUE_TUPLE);
      auto tuple_value = reinterpret_cast<const expression::TupleValueExpression *>(hashkey.get());
      // Debug by Michael
      oid_t cid = tuple_value->GetColumnId();
      std::cout << cid;
      // end debug
      column_ids_.push_back(tuple_value->GetColumnId());
    }

    // Construct the hash table by going over each child logical tile and hashing
    for (size_t child_tile_itr = 0; child_tile_itr < child_tiles_.size(); child_tile_itr++) {
      auto tile = child_tiles_[child_tile_itr].get();

      // Go over all tuples in the logical tile
      for (oid_t tuple_id : *tile) {
        // Key : container tuple with a subset of tuple attributes
        // Value : < child_tile offset, tuple offset >
        htable_[HashMapType::key_type(tile, tuple_id, &column_ids_)].insert(std::make_pair(child_tile_itr, tuple_id));
      }

    }

    done_ = true;
  }

  // Print hash table for debugging
  DumpHashTable();

  // Return logical tiles one at a time
  while (result_itr < child_tiles_.size()) {
    if (child_tiles_[result_itr]->GetTupleCount() == 0) {
      result_itr++;
      continue;
    } else {
      SetOutput(child_tiles_[result_itr++].release());
      return true;
    }
  }

  return false;
}


void HashExecutor::DumpHashTable() const {
  assert(done_);

  // Go over hash table
  for (__attribute__((unused)) auto &kv : htable_) {
	size_t code = kv.first.HashCode();
	size_t sz = kv.second.size();
	std::cout << code << sz;
    LOG_INFO("Key %lu, Num of tuple: %lu", kv.first.HashCode(), kv.second.size());
  }

  int n = htable_.size();
  std::cout << n;
}

} /* namespace executor */
} /* namespace peloton */
