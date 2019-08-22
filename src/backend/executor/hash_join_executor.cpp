//===----------------------------------------------------------------------===//
//
//                         PelotonDB
//
// hash_join_executor.cpp
//
// Identification: src/backend/executor/hash_join_executor.cpp
//
// Copyright (c) 2015, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#include <vector>

#include "backend/common/types.h"
#include "backend/common/logger.h"
#include "backend/executor/logical_tile_factory.h"
#include "backend/executor/hash_join_executor.h"
#include "backend/expression/abstract_expression.h"
#include "backend/expression/container_tuple.h"

namespace peloton {
namespace executor {

/**
 * @brief Constructor for hash join executor.
 * @param node Hash join node corresponding to this executor.
 */
HashJoinExecutor::HashJoinExecutor(const planner::AbstractPlan *node,
                                   ExecutorContext *executor_context)
    : AbstractJoinExecutor(node, executor_context) {}

bool HashJoinExecutor::DInit() {
  assert(children_.size() == 2);

  auto status = AbstractJoinExecutor::DInit();
  if (status == false) return status;

  assert(children_[1] != nullptr);
  assert(children_[1]->GetRawNode()->GetPlanNodeType() == PLAN_NODE_TYPE_HASH);

  hash_executor_ = reinterpret_cast<HashExecutor *>(children_[1]);

  return true;
}

/**
 * @brief Creates logical tiles from the two input logical tiles after applying
 * join predicate.
 * @return true on success, false otherwise.
 */
bool HashJoinExecutor::DExecute() {
  LOG_INFO("********** Hash %s Join executor :: 2 children ",
           GetJoinTypeString());

  // Loop until we have non-empty result join logical tile or exit
  for (;;) {
    // check if there is any tile in the buffer
    if (!buffered_output_tiles.empty()) {
      SetOutput(buffered_output_tiles.front());
      buffered_output_tiles.pop_front();
      return true;
    }

    // Build outer join output when done
    if (left_child_done_ && right_child_done_) {
      return BuildOuterJoinOutput();
    }

    //===--------------------------------------------------------------------===//
    // Pick right and left tiles
    //===--------------------------------------------------------------------===//

    // Get all the logical tiles from RIGHT child
    if (!right_child_done_) {
      while (children_[1]->Execute()) {
        LOG_TRACE("Retrieve a new tile from right child");
        BufferRightTile(children_[1]->GetOutput());
      }

      if (right_result_tiles_.empty()) {
        LOG_TRACE("Right child returned nothing. Exit.");
        return false;
      }

      LOG_TRACE("Right child is exhausted");
      right_child_done_ = true;
    }

    // Get next logical tile from LEFT child
    if (children_[0]->Execute() == false) {
      LOG_TRACE("Left child is exhausted");

      if (left_result_tiles_.empty()) {
        LOG_TRACE("Left child returned nothing. Exit.");
        return false;
      }

      left_child_done_ = true;
      return BuildOuterJoinOutput();
    }
    else {
      LOG_TRACE("Advance the left child");
      BufferLeftTile(children_[0]->GetOutput());
    }

    //===--------------------------------------------------------------------===//
    // Build Join Tile
    //===--------------------------------------------------------------------===//
    assert(left_result_tiles_.size() > 0);
    LogicalTile *left_tile = left_result_tiles_.back().get();

    // Get the hash table from the hash executor
    auto &hash_table = hash_executor_->GetHashTable();
    auto &hashed_col_ids = hash_executor_->GetHashKeyIds();

    // Output tile cache
    oid_t prev_tile_id = INVALID_OID;
    std::unique_ptr<LogicalTile> output_tile;
    LogicalTile::PositionListsBuilder pos_lists_builder;

    // Go over the left logical tile
    for (const auto left_tuple_id : *left_tile) {
      bool has_right_match = false;

      const expression::ContainerTuple<executor::LogicalTile> left_tuple(
        left_tile, left_tuple_id, &hashed_col_ids);

      // get the hash values for the left tuple
      const auto right_tuples = hash_table.find(left_tuple);

      if (right_tuples == hash_table.end()) {
        continue;
      }

      // For each tuple, find matching tuples in the hash table
      // built on top of the right table
      // Go over the matching right tuples
      for (const auto &right_tuple: right_tuples->second) {
        auto right_tile_id = right_tuple.first;
        auto right_tuple_id = right_tuple.second;
	assert(right_tile_id >= 0 && right_tile_id < right_result_tiles.size());

        LogicalTile *right_tile = right_result_tiles_[right_tile_id].get();

        // Join predicate exists
        if (predicate_ != nullptr) {
          expression::ContainerTuple<executor::LogicalTile> left_tuple(
              left_tile, left_tuple_id);
          expression::ContainerTuple<executor::LogicalTile> right_tuple(
              right_tile, right_tuple_id);

          // Join predicate is false. Skip pair and continue.
          if (predicate_->Evaluate(&left_tuple, &right_tuple, executor_context_)
              .IsFalse()) {
            continue;
          }
        }

        if (prev_tile_id != right_tile_id) {
          // Save the output tile cache into the buffer
          if (pos_lists_builder.Size() > 0) {
            output_tile->SetPositionListsAndVisibility(pos_lists_builder.Release());
            buffered_output_tiles.push_back(output_tile.release());
          }

          // Build output join logical tile
          output_tile = BuildOutputLogicalTile(left_tile, right_tile);

          // Build position lists
          pos_lists_builder =
              LogicalTile::PositionListsBuilder(left_tile, right_tile);

          pos_lists_builder.SetRightSource(
              &right_result_tiles_[right_tile_id]->GetPositionLists());
        }
        prev_tile_id = right_tile_id;

        // For Left and Full Outer Join
        has_right_match = true;

        // Add join tuple
        RecordMatchedRightRow(right_tile_id, right_tuple_id);

        // Insert a tuple into the output logical tile
        // copy the elements in left logical tile's tuple
        pos_lists_builder.AddRow(left_tuple_id, right_tuple_id);
      }

      // Left and Full Outer Join
      if (has_right_match) {
        RecordMatchedLeftRow(left_result_tiles_.size() - 1, left_tuple_id);
      }
    } // loop right_tuple : right_tuples

    // Check if we have any join tuples
    if (pos_lists_builder.Size() > 0) {
      output_tile->SetPositionListsAndVisibility(pos_lists_builder.Release());
      buffered_output_tiles.push_back(output_tile.release());
    } else {
      LOG_TRACE("This left tile produces empty join result. Continue the loop.");
    }
  } // loop left_tuple_id : *left_tile
}

}  // namespace executor
}  // namespace peloton
