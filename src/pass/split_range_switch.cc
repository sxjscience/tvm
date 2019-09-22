/*
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

/*!
 *  Copyright (c) 2019 by Contributors
 * \file split_range_switch.cc
 */
#include <tvm/ir.h>
#include <tvm/ir_visitor.h>
#include <tvm/ir_mutator.h>
#include <tvm/ir_pass.h>
#include <tvm/arithmetic.h>
#include <unordered_map>
#include <unordered_set>

namespace tvm {
namespace ir {

class RangeSwitchSelector final : public IRVisitor {
  public:
    void Visit_(const Call* op) {
      if (op->is_intrinsic(intrinsic::tvm_range_switch)) {
        call_record_.push_back(op);
      }
      IRVisitor::Visit_(op);
    }

    void Visit_(const For* op) {
      if (op->for_type == ForType::RangeSplit) {
        std::cout << "For!" << std::endl;
        for_record_.push_back(op);
      }
      IRVisitor::Visit_(op);
    }

    void Visit_(const Block* op) {
      std::cout << "First:" << std::endl;
      std::cout << op->first << std::endl;
      std::cout << "Rest:" << std::endl;
      std::cout << op->rest << std::endl;
      IRVisitor::Visit_(op);
    }
    std::vector<const Call*> call_record_;
    std::vector<const For*> for_record_;
};

class RangeSwitchSplitter final : public IRMutator {
  public:


  private:
    RangeSwitchSelector selector;

};

Stmt SplitRangeSwitch(Stmt stmt) {
  RangeSwitchSelector selector;
  selector.Visit(stmt);
  std::cout << selector.call_record_.size() << std::endl;
  std::cout << selector.for_record_.size() << std::endl;
  for (auto op: selector.call_record_) {
    std::cout << "tvm_iter_var:" << op->args[0] << std::endl;
    for (auto for_op : selector.for_record_) {
      if (Equal(op->args[0], for_op->loop_var)) {
        std::cout << "Found!" << op->args[0] << std::endl;
      }
    }
  }
  return stmt;
}

}  // namespace ir
}  // namespace tvm