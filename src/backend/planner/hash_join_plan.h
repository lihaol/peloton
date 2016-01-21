//===----------------------------------------------------------------------===//
//
//                         PelotonDB
//
// hash_join_node.h
//
// Identification: src/backend/planner/hash_join_node.h
//
// Copyright (c) 2015, Carnegie Mellon University Database Group
//
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "backend/common/types.h"
#include "backend/expression/abstract_expression.h"
#include "backend/planner/abstract_join_plan.h"
#include "backend/planner/project_info.h"

namespace peloton {
namespace planner {

class HashJoinPlan : public AbstractJoinPlan {
 public:

  HashJoinPlan(const HashJoinPlan &) = delete;
  HashJoinPlan &operator=(const HashJoinPlan &) = delete;
  HashJoinPlan(HashJoinPlan &&) = delete;
  HashJoinPlan &operator=(HashJoinPlan &&) = delete;

  HashJoinPlan(const expression::AbstractExpression *predicate,
                const ProjectInfo *proj_info, const std::vector<oid_t> &outer_hashkeys)
  : AbstractJoinPlan(JOIN_TYPE_INVALID, predicate, proj_info) {
	  outer_column_ids_ = outer_hashkeys; // added by Michael
  }

  inline PlanNodeType GetPlanNodeType() const {
    return PLAN_NODE_TYPE_HASHJOIN;
  }


  inline std::string GetInfo() const { return "HashJoin"; }

  const std::vector<oid_t> &GetOuterHashIds() const {
    return outer_column_ids_;
  }

 private:

  std::vector<oid_t> outer_column_ids_;
};

}  // namespace planner
}  // namespace peloton
