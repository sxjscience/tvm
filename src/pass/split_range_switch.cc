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
#include "../arithmetic/int_set.h"
#include "../runtime/thread_storage_scope.h"

namespace tvm {
namespace ir {

class RangeSwitchSelector final : public IRVisitor {
  public:
    void Visit_(const Call* op) {
      // partition const loop when sets split_const_loop_
      std::cout << op->name << std::endl;
      if(op->is_intrinsic(intrinsic::tvm_range_switch)) {
        record_[op] = 0;
      }
    }
    std::unordered_map<const Call*, int> record_;
};

class RangeSwitchSplitter final : public IRMutator {
  public:
    RangeSwitchSelector selector;
};

Stmt SplitRangeSwitch(Stmt stmt) {
  RangeSwitchSelector selector;
  selector.Visit(stmt);
  return stmt;
}

}  // namespace ir
}  // namespace tvm