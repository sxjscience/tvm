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
 * \file optimize_range_switch.cc
 */
#include <tvm/ir.h>
#include <tvm/ir_visitor.h>
#include <tvm/ir_mutator.h>
#include <tvm/ir_pass.h>
#include <tvm/arithmetic.h>
#include <tvm//expr_operator.h>
#include <unordered_map>
#include <unordered_set>

namespace tvm {
namespace ir {

//class RangeSwitchSelector final : public IRVisitor {
//  public:
//    void Visit_(const Call* op) {
//      if (op->is_intrinsic(intrinsic::tvm_range_switch)) {
//        call_record_.insert(op);
//      }
//      IRVisitor::Visit_(op);
//    }
//
//    void Visit_(const For* op) {
//      if (op->for_type == ForType::RangeSplit) {
//        auto it = range_switch_split_info_.find(op);
//        CHECK_EQ(it, range_switch_split_info_.end());
//        range_switch_split_info_.insert({op, nullptr});
//      }
//      IRVisitor::Visit_(op);
//    }
//
//    std::unordered_set<const Call*> no_split_nodes_;
//    std::unordered_map<const For*, const Call*> range_switch_split_info_;
//};

class RangeSwitchOpMerger final : public IRMutator {
  public:
    // Binary Ops
    Expr Mutate_(const Add* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const Sub* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const Mul* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const Div* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const FloorDiv* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const FloorMod* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const Mod* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const Min* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const Max* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const EQ* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const NE* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const LT* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const LE* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const GT* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const GE* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const And* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const Or* op, const Expr& e) final { return BinaryOpMutate_(op, e); }
    Expr Mutate_(const Cast* op, const Expr& e) final {
      Expr value = this->Mutate(op->value);
      const Call* value_op = value.as<Call>();
      if(value_op && value_op->is_intrinsic(intrinsic::tvm_range_switch)) {
        Expr idx = value_op->args[0];
        Array<Expr> uppers;
        Array<Expr> values;
        int value_num = value_op->args.size() / 2;
        for (int i = 1; i < value_num; i++) {
          uppers.push_back(value_op->args[i]);
        }
        for (int i = value_num; i < 2 * value_num; i++) {
          values.push_back(Cast::make(op->type, value_op->args[i]));
        }
        return range_switch(idx, uppers, values);
      }
      if (value.same_as(op->value)) {
        return e;
      } else {
        return Cast::make(op->type, value);
      }
    }
  private:
    template<typename OP>
    Expr BinaryOpMutate_(const OP* op, const Expr& e) {
      Expr a = this->Mutate(op->a);
      Expr b = this->Mutate(op->b);
      const Call* a_op = a.as<Call>();
      const Call* b_op = b.as<Call>();
      if (a_op) {
        if (a_op->is_intrinsic(intrinsic::tvm_range_switch)) {
          Expr idx = a_op->args[0];
          Array<Expr> uppers;
          Array<Expr> values;
          int value_num = a_op->args.size() / 2;
          for (int i = 1; i < value_num; i++) {
            uppers.push_back(a_op->args[i]);
          }
          for (int i = value_num; i < 2 * value_num; i++) {
            values.push_back(OP::make(a_op->args[i], b));
          }
          return range_switch(idx, uppers, values);
        }
      } else if (b_op) {
        if (b_op->is_intrinsic(intrinsic::tvm_range_switch)) {
          Expr idx = b_op->args[0];
          Array<Expr> uppers;
          Array<Expr> values;
          int value_num = b_op->args.size() / 2;
          for (int i = 1; i < value_num; i++) {
            uppers.push_back(b_op->args[i]);
          }
          for (int i = value_num; i < 2 * value_num; i++) {
            values.push_back(OP::make(a, b_op->args[i]));
          }
          return range_switch(idx, uppers, values);
        }
      }
      if (a.same_as(op->a) && b.same_as(op->b)) {
        return e;
      } else {
        return OP::make(a, b);
      }
    }
};


class RangeSwitchSplitter final : public IRMutator {
  public:
    Stmt Mutate_(const Store* op, const Stmt& s) final {
      return IRMutator::Mutate_(op, s);
    }
};


class RangeSwitchRewriter final : public IRMutator {
  public:
    Expr Mutate_(const Call* op, const Expr& e) final {
      if (op->is_intrinsic(intrinsic::tvm_range_switch)) {
        int value_num = op->args.size() / 2;
        CHECK_GT(value_num, 1);
        Expr ret = this->Mutate(op->args[2 * value_num - 1]); // Default value
        Expr idx = this->Mutate(op->args[0]);
        for (int i = value_num - 1; i >= 1; i--) {
          Expr upper = this->Mutate(op->args[i]);
          Expr value = this->Mutate(op->args[i + value_num - 1]);
          ret = if_then_else(idx < upper, value, ret);
          std::cout << ret << std::endl;
        }
        return ret;
      } else {
        return IRMutator::Mutate_(op, e);
      }
    }
};
//class RangeSwitchSplitter final : public IRMutator {
//  public:
//    Stmt VisitAndMutate(const Stmt& stmt) {
//      selector.Visit(stmt);
//      for (auto call_op : selector.call_record_) {
//        for (auto for_op : selector.for_record_) {
//          if (Equal(call_op->args[0], for_op->loop_var)) {
//            auto it = for_split_info_.find(for_op);
//            if (for_split_info_.find())
//          }
//        }
//      }
//      return Mutate(stmt);
//    }
//
//    Expr Mutate_(const Call* op) {
//      if (op->is_intrinsic(intrinsic::tvm_range_switch)) {
//
//      }
//    }
//
//    Expr Mutate_(const For* op, const Stmt& s) {
//      // Store the For Node
//      if (op->for_type == ForType::RangeSplit) {
//
//        Stmt ret = IRMutator::Mutate_(op, s);
//      } else {
//        return IRMutator::Mutate_(op, s);
//      }
//    }
//
//  private:
//    std::unordered_set<const Call*> intrin_split_nodes_;
//    std::unordered_map<const For*, const Call*> for_split_info_;
//    RangeSwitchSelector selector;
//
//};

Stmt PushOpInsideRangeSwitch(Stmt stmt) {
  RangeSwitchOpMerger merger;
  return merger.Mutate(stmt);
}

Stmt SplitRangeSwitch(Stmt stmt) {
  return stmt;
}

Stmt RewriteRangeSwitch(Stmt stmt) {
  RangeSwitchRewriter rewriter;
  return rewriter.Mutate(stmt);
}

}  // namespace ir
}  // namespace tvm