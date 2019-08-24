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
 * \file expr_operator.cc
 */
#include <tvm/base.h>
#include <tvm/ir.h>
#include <tvm/expr_operator.h>
#include <cmath>
// Centralized header for constant folders.
#include "../arithmetic/const_fold.h"

namespace tvm {

// simple cast that only checks if type matches and cast
inline Expr SimpleCast(const Type& t, Expr value) {
  if (value.type() == t) return value;
  return ir::Cast::make(t, value);
}

// The public function with a quick checking path.
void BinaryOpMatchTypes(Expr& lhs, Expr& rhs) {  // NOLINT(*)
  if (lhs.type() == rhs.type()) return;
  Type ltype = lhs.type();
  Type rtype = rhs.type();
  if (ltype.lanes() == 1 && rtype.lanes() != 1) {
    lhs = ir::Broadcast::make(lhs, rtype.lanes());
  } else if (rtype.lanes() == 1 && ltype.lanes() != 1) {
    rhs = ir::Broadcast::make(rhs, ltype.lanes());
  } else {
    CHECK(ltype.lanes() == rtype.lanes())
        << "Cannot match type " << ltype << " vs " << rtype;
  }
  if (lhs.type() == rhs.type()) return;
  // Only do very simple type coversion
  // int->float, int(32)->int(64)
  // require the types to be relatively consistent
  // This will the reduce amount code generated by operators
  // and also help user to find potential type conversion problems.
  if (!lhs.type().is_float() && rhs.type().is_float()) {
    // int->float
    lhs = cast(rhs.type(), lhs);
  } else if (lhs.type().is_float() && !rhs.type().is_float()) {
    // int->float
    rhs = cast(lhs.type(), rhs);
  } else if ((lhs.type().is_int() && rhs.type().is_int()) ||
             (lhs.type().is_uint() && rhs.type().is_uint())) {
    // promote int to higher bits
    if (lhs.type().bits() < rhs.type().bits()) {
      lhs = cast(rhs.type(), lhs);
    } else {
      rhs = cast(lhs.type(), rhs);
    }
  } else if ((lhs.type().is_int() && rhs.type().is_uint()) ||
             (lhs.type().is_uint() && rhs.type().is_int())) {
    int bits = std::max(lhs.type().bits(), rhs.type().bits());
    lhs = SimpleCast(Int(bits, lhs.type().lanes()), lhs);
    rhs = SimpleCast(Int(bits, rhs.type().lanes()), rhs);
  } else {
    LOG(FATAL) << "Cannot match type " << ltype << " vs " << rtype;
  }
}

// public function to check whether the expressions within the array have the same type
// Also, broadcast the lanes of the inner expressions if possible.
void ArrayOpMatchTypes(Array<Expr> &arr) {
  if (arr.size() == 0) return;
  int bcast_lanes = 1;
  for (int i = 0; i < arr.size(); i++) {
    int lanes = arr[i].type().lanes();
    if (lanes != 1) {
      if (bcast_lanes != 1 && lanes != bcast_lanes) {
        LOG(FATAL) << "Cannot match the type of the arr. Fail at the idx="
                   << i << ". Found lanes=" << lanes << " while the expected=" << bcast_lanes;
      } else {
        bcast_lanes = lanes;
      }
    }
  }
  // Broadcast the lanes of the inner expressions
  for (int i = 0; i < arr.size(); i++) {
    int lanes = arr[i].type().lanes();
    if (lanes == 1) {
      arr.Set(i, ir::Broadcast::make(arr[i], bcast_lanes));
    }
  }
  // Type checking, we do not do any data type conversion
  Type base_type = arr[0].type();
  for (int i = 0; i < arr.size(); i++) {
    CHECK_EQ(arr[i].type(), base_type) << "Type mismatch, "
                                          << " arr[0].type=" << base_type
                                          << " arr[" << i << "].type=" << arr[i].type();
  }
}

// Function that matches the types of indices in RangeSwitch.
// Automatically cast the inner data types to the consistent integer types.
void MatchRangeSwitchIdxTypes(Expr &idx, Array<Expr> &uppers) {
  CHECK_EQ(idx.type().lanes(), 1) << "Only unvectorized types are supported. idx.lanes="
                                       << idx.type().lanes();
  for (const auto& expr : uppers) {
    CHECK_EQ(expr.type().lanes(), 1) << "Only unvectorized types are supported. expr.lanes="
                                          << expr.type().lanes();
  }
  // Distinguish
  bool all_integer_dtype = true;
  all_integer_dtype = all_integer_dtype && (idx.type().is_int() || idx.type().is_uint());
  for (const auto& expr : uppers) {
    if (all_integer_dtype) {
      all_integer_dtype = all_integer_dtype && (expr.type().is_int() || expr.type().is_uint());
    } else {
      break;
    }
  }
  // Try to automatically cast the types of idx and uppers to the same data type if they have the
  // integer types
  if (all_integer_dtype) {
    bool use_int = idx.type().is_int();
    int max_bits = idx.type().bits();
    Type dtype = idx.type();
    for (int i = 0; i < uppers.size(); i++) {
      const Type& ele_type = uppers[i].type();
      CHECK((ele_type.is_int() || ele_type.is_uint())
            && ele_type.lanes() == 1) << "Only Integer unvectorized type is supported."
                                      << " uppers[" << i << "].type = " << ele_type;
      use_int = (use_int || ele_type.is_int());
      max_bits = std::max(max_bits, ele_type.bits());
    }
    if (use_int) {
      dtype = Int(max_bits, 1);
    } else {
      dtype = UInt(max_bits, 1);
    }
    idx = SimpleCast(dtype, idx);
    for (int i = 0; i < uppers.size(); i++) {
      if (uppers[i].type() != dtype) {
        uppers.Set(i, SimpleCast(dtype, uppers[i]));
      }
    }
  }
}

// Function that returns the index of the range that the idx falls into.
// returns -1 if not found and returns uppers.size() if idx is larger than all the ranges.
template<typename T>
int EagerEvaluateRangeSwitch(const Expr& idx, const Array<Expr>& uppers) {
  bool cannot_eager_eval = false;
  const T* op = idx.as<T>();
  CHECK(op != nullptr) << "Type mismatch, the given idx cannot be casted to the specified type.";
  auto idx_val = op->value;
  for (int i = 0; i < uppers.size(); i++) {
    if (const T* upper_op = uppers[i].as<T>()) {
      if (idx_val < upper_op->value) {
        return i;
      }
    } else {
      cannot_eager_eval = true;
      break;
    }
  }
  if (!cannot_eager_eval) {
    return uppers.size();
  } else {
    return -1;
  }
}

template<typename ValueType>
inline bool ConstPowerHelper(ValueType val, int *shift) {
  if (val <= 0) return false;
  shift[0] = 0;
  while (val != 0) {
    if (val & 1) {
      return (val == 1);
    }
    ++shift[0];
    val = val >> 1;
  }
  return true;
}

bool is_const_power_of_two_integer(const Expr& x, int* shift) {
  if (const auto* op = x.as<ir::IntImm>()) {
    return ConstPowerHelper(op->value, shift);
  } else if (const auto* op = x.as<ir::UIntImm>()) {
    return ConstPowerHelper(op->value, shift);
  } else {
    return false;
  }
}

Expr cast(const Type& t, Expr value) {
  using ir::IntImm;
  using ir::UIntImm;
  using ir::FloatImm;
  if (value.type() == t) return value;
  // const fold IntImm as they are used in index computations
  if (t.lanes() == 1) {
    if (const IntImm* op = value.as<IntImm>()) {
      return make_const(t, op->value);
    } else if (const UIntImm* op = value.as<UIntImm>()) {
      return make_const(t, op->value);
    } else if (const FloatImm* op = value.as<FloatImm>()) {
      return make_const(t, op->value);
    }
    return ir::Cast::make(t, value);
  } else {
    if (value.type().lanes() == 1) {
      // manually unroll cast
      Type vtype = t.element_of();
      if (value.type() != vtype) {
        if (const IntImm* op = value.as<IntImm>()) {
          value = make_const(vtype, op->value);
        } else if (const UIntImm* op = value.as<UIntImm>()) {
          return make_const(t, op->value);
        } else if (const FloatImm* op = value.as<FloatImm>()) {
          value = make_const(vtype, op->value);
        } else {
          value = ir::Cast::make(vtype, value);
        }
      }
      return ir::Broadcast::make(value, t.lanes());
    } else {
      CHECK(value.type().lanes() == t.lanes());
      return ir::Cast::make(t, value);
    }
  }
}

Expr reinterpret(const Type& t, Expr value) {
  if (value.type() == t) return value;
  return ir::Call::make(t, ir::Call::reinterpret, { value }, ir::Call::PureIntrinsic);
}

Expr operator+(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::Add>(a, b);
  if (ret.defined()) return ret;
  return ir::Add::make(a, b);
}

// negation
Expr operator-(Expr a) {
  using ir::IntImm;
  using ir::FloatImm;
  const IntImm* pa = a.as<IntImm>();
  const FloatImm* fa = a.as<FloatImm>();
  if (pa) return ir::IntImm::make(a.type(), -pa->value);
  if (fa) return ir::FloatImm::make(a.type(), -fa->value);
  return make_zero(a.type()) - a;
}

Expr operator-(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::Sub>(a, b);
  if (ret.defined()) return ret;
  return ir::Sub::make(a, b);
}

Expr operator*(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::Mul>(a, b);
  if (ret.defined()) return ret;
  return ir::Mul::make(a, b);
}

Expr operator/(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::Div>(a, b);
  if (ret.defined()) return ret;
  return ir::Div::make(a, b);
}

Expr operator%(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::Mod>(a, b);
  if (ret.defined()) return ret;
  return ir::Mod::make(a, b);
}

Expr floordiv(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::FloorDiv>(a, b);
  if (ret.defined()) return ret;
  return ir::FloorDiv::make(a, b);
}

Expr floormod(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::FloorMod>(a, b);
  if (ret.defined()) return ret;
  return ir::FloorMod::make(a, b);
}

Expr min(Expr a, Expr b) {
  // inf-aware simplificaiton
  using arith::is_pos_inf;
  using arith::is_neg_inf;
  if (is_pos_inf(a)) return b;
  if (is_neg_inf(a)) return a;
  if (is_pos_inf(b)) return a;
  if (is_neg_inf(b)) return b;
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::Min>(a, b);
  if (ret.defined()) return ret;
  return ir::Min::make(a, b);
}

Expr max(Expr a, Expr b) {
  // inf-aware simplificaiton
  using arith::is_pos_inf;
  using arith::is_neg_inf;
  if (is_pos_inf(a)) return a;
  if (is_neg_inf(a)) return b;
  if (is_pos_inf(b)) return b;
  if (is_neg_inf(b)) return a;
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::Max>(a, b);
  if (ret.defined()) return ret;
  return ir::Max::make(a, b);
}

Expr if_then_else(Expr cond, Expr true_value, Expr false_value) {
  using ir::IntImm;
  using ir::UIntImm;
  CHECK(cond.type() == Bool(1))
      << "if_then_else only accept the condition to be boolean type.";
  BinaryOpMatchTypes(true_value, false_value);
  if (const UIntImm* op = cond.as<UIntImm>()) {
    if (op->value != 0) {
      return true_value;
    } else {
      return false_value;
    }
  } else if (const IntImm* op = cond.as<IntImm>()) {
    if (op->value != 0) {
      return true_value;
    } else {
      return false_value;
    }
  }
  return ir::Call::make(
      true_value.type(),
      ir::intrinsic::tvm_if_then_else,
      {cond, true_value, false_value},
      ir::Call::PureIntrinsic);
}

Expr range_switch(Expr idx, Array<Expr> uppers, Array<Expr> values) {
  using ir::IntImm;
  using ir::UIntImm;
  CHECK_EQ(uppers.size() + 1, values.size())
    << "The number of range upper bounds needs to be the same as the number of expressions."
    << "uppers.size() = " << uppers.size() <<" values.size() = " << values.size();
  CHECK_GT(values.size(), 0) << "The values array cannot be empty.";
  if (uppers.empty()) {
    return values[values.size() - 1];
  }
  ArrayOpMatchTypes(values);
  // Try to derive the results if the idx has int types and the value falls inside the range
  // determined by uppers.
  MatchRangeSwitchIdxTypes(idx, uppers);
  int eager_sel = -1;
  if (idx.get()->is_type<IntImm>()) {
    eager_sel = EagerEvaluateRangeSwitch<IntImm>(idx, uppers);
  } else if (idx.get()->is_type<UIntImm>()) {
    eager_sel = EagerEvaluateRangeSwitch<UIntImm>(idx, uppers);
  }
  if (eager_sel >= 0) {
    return values[eager_sel];
  } else {
    Array<Expr> make_args;
    make_args.push_back(idx);
    for (const auto& expr : uppers) {
      make_args.push_back(expr);
    }
    for (const auto& expr : values) {
      make_args.push_back(expr);
    }
    return ir::Call::make(
            values[values.size() - 1].type(),
            ir::intrinsic::tvm_range_switch,
            make_args,
            ir::Call::PureIntrinsic);
  }
}

Expr likely(Expr cond) {
  if (is_const(cond)) return cond;
  return ir::Call::make(cond.type(), ir::Call::likely, { cond }, ir::Call::PureIntrinsic);
}

Expr operator>(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::GT>(a, b);
  if (ret.defined()) return ret;
  return ir::GT::make(a, b);
}

Expr operator>=(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::GE>(a, b);
  if (ret.defined()) return ret;
  return ir::GE::make(a, b);
}

Expr operator<(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::LT>(a, b);
  if (ret.defined()) return ret;
  return ir::LT::make(a, b);
}

Expr operator<=(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::LE>(a, b);
  if (ret.defined()) return ret;
  return ir::LE::make(a, b);
}

Expr operator==(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::EQ>(a, b);
  if (ret.defined()) return ret;
  return ir::EQ::make(a, b);
}

Expr operator!=(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  Expr ret = arith::TryConstFold<ir::NE>(a, b);
  if (ret.defined()) return ret;
  return ir::NE::make(a, b);
}

Expr operator&&(Expr a, Expr b) {
  CHECK(a.type().is_bool());
  CHECK(b.type().is_bool());
  Expr ret = arith::TryConstFold<ir::And>(a, b);
  if (ret.defined()) return ret;
  return ir::And::make(a, b);
}

Expr operator||(Expr a, Expr b) {
  CHECK(a.type().is_bool());
  CHECK(b.type().is_bool());
  Expr ret = arith::TryConstFold<ir::Or>(a, b);
  if (ret.defined()) return ret;
  return ir::Or::make(a, b);
}

Expr operator!(Expr a) {
  CHECK(a.type().is_bool());
  Expr ret = arith::TryConstFold<ir::Not>(a);
  if (ret.defined()) return ret;
  return ir::Not::make(a);
}

Expr operator>>(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  TVM_INDEX_CONST_PROPAGATION({
      const Type& rtype = a.type();
      if (pa && pb) return IntImm::make(rtype, (pa->value >> pb->value));
      if (pb) {
        if (pb->value == 0) return a;
      }
    });
  return ir::Call::make(a.type(), ir::Call::shift_right, { a, b }, ir::Call::PureIntrinsic);
}

Expr operator<<(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  TVM_INDEX_CONST_PROPAGATION({
      const Type& rtype = a.type();
      if (pa && pb) return IntImm::make(rtype, (pa->value << pb->value));
      if (pb) {
        if (pb->value == 0) return a;
      }
    });
  return ir::Call::make(a.type(), ir::Call::shift_left, { a, b }, ir::Call::PureIntrinsic);
}

Expr operator&(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  TVM_INDEX_CONST_PROPAGATION({
      const Type& rtype = a.type();
      if (pa && pb) return IntImm::make(rtype, (pa->value & pb->value));
    });
  return ir::Call::make(a.type(), ir::Call::bitwise_and, { a, b }, ir::Call::PureIntrinsic);
}

Expr operator|(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  TVM_INDEX_CONST_PROPAGATION({
      const Type& rtype = a.type();
      if (pa && pb) return IntImm::make(rtype, (pa->value | pb->value));
    });
  return ir::Call::make(a.type(), ir::Call::bitwise_or, { a, b }, ir::Call::PureIntrinsic);
}

Expr operator^(Expr a, Expr b) {
  BinaryOpMatchTypes(a, b);
  TVM_INDEX_CONST_PROPAGATION({
      const Type& rtype = a.type();
      if (pa && pb) return IntImm::make(rtype, (pa->value ^ pb->value));
    });
  return ir::Call::make(a.type(), ir::Call::bitwise_xor, { a, b }, ir::Call::PureIntrinsic);
}

Expr operator~(Expr a) {
  CHECK(a.type().is_int() || a.type().is_uint());
  return ir::Call::make(a.type(), ir::Call::bitwise_not, { a }, ir::Call::PureIntrinsic);
}

Expr pow(Expr x, Expr y) {
  BinaryOpMatchTypes(x, y);
  CHECK(x.type().is_float()) << "power only applies to float";
  return ir::Call::make(x.type(), "pow", { x, y }, ir::Call::PureIntrinsic);
}

Expr abs(Expr x) {
  if (x.type().is_int()) {
    using ir::IntImm;
    const IntImm* px = x.as<IntImm>();
    if (px) {
      return ir::IntImm::make(x.type(), std::abs(px->value));
    }
    return ir::Select::make(x >= make_zero(x.type()), x, -x);
  } else if (x.type().is_float()) {
    using ir::FloatImm;
    const FloatImm* fx = x.as<FloatImm>();
    if (fx) {
      return ir::FloatImm::make(x.type(), std::fabs(fx->value));
    }
    return ir::Call::make(x.type(), "fabs", {x}, ir::Call::PureIntrinsic);
  } else if (x.type().is_uint()) {
    return x;
  } else {
    LOG(FATAL) << "Data type " << x.type()
               <<" not supported for absolute op. Skipping absolute op...";
    return x;
  }
}

Expr sum(Expr source, Array<IterVar> rdom) {
  Var x("x", source.type()), y("y", source.type());
  Expr result = ir::Add::make(x, y);
  Expr identity_element = make_zero(source.type());
  ir::CommReducer combiner =
    ir::CommReducerNode::make({x}, {y}, {result}, {identity_element});
  return ir::Reduce::make(combiner, {source}, rdom, make_const(Bool(1), true), 0);
}

Expr all(Expr source, Array<IterVar> rdom) {
  CHECK(source.type().is_bool());
  Var x("x", source.type()), y("y", source.type());
  Expr result = ir::And::make(x, y);
  Expr identity_element = make_const(source.type(), true);
  ir::CommReducer combiner =
    ir::CommReducerNode::make({x}, {y}, {result}, {identity_element});
  return ir::Reduce::make(combiner, {source}, rdom, make_const(Bool(1), true), 0);
}

Expr max(Expr source, Array<IterVar> rdom) {
  Var x("x", source.type()), y("y", source.type());
  Expr result = ir::Max::make(x, y);
  Expr identity_element = source.type().min();
  ir::CommReducer combiner =
    ir::CommReducerNode::make({x}, {y}, {result}, {identity_element});
  return ir::Reduce::make(combiner, {source}, rdom, make_const(Bool(1), true), 0);
}

Expr min(Expr source, Array<IterVar> rdom) {
  Var x("x", source.type()), y("y", source.type());
  Expr result = ir::Min::make(x, y);
  Expr identity_element = source.type().max();
  ir::CommReducer combiner =
    ir::CommReducerNode::make({x}, {y}, {result}, {identity_element});
  return ir::Reduce::make(combiner, {source}, rdom, make_const(Bool(1), true), 0);
}

Expr prod(Expr source, Array<IterVar> rdom) {
  Var x("x", source.type()), y("y", source.type());
  Expr result = ir::Mul::make(x, y);
  Expr identity_element = make_const(source.type(), 1);
  ir::CommReducer combiner =
    ir::CommReducerNode::make({x}, {y}, {result}, {identity_element});
  return ir::Reduce::make(combiner, {source}, rdom, make_const(Bool(1), true), 0);
}

Expr fmod(Expr x, Expr y) {
  BinaryOpMatchTypes(x, y);
  CHECK(x.type().is_float()) << "fmod only applies to float";
  return ir::Call::make(x.type(), "fmod", { x, y }, ir::Call::PureIntrinsic);
}

Expr floor(Expr x) {
  using ir::FloatImm;
  const FloatImm* fx = x.as<FloatImm>();
  if (fx) return FloatImm::make(x.type(), std::floor(fx->value));
  return ir::Call::make(x.type(), "floor", {x}, ir::Call::PureIntrinsic);
}

Expr ceil(Expr x) {
  using ir::FloatImm;
  const FloatImm* fx = x.as<FloatImm>();
  if (fx) return FloatImm::make(x.type(), std::ceil(fx->value));
  return ir::Call::make(x.type(), "ceil", {x}, ir::Call::PureIntrinsic);
}

Expr round(Expr x) {
  using ir::FloatImm;
  const FloatImm* fx = x.as<FloatImm>();
  if (fx) return FloatImm::make(x.type(), std::nearbyint(fx->value));
  return ir::Call::make(x.type(), "round", {x}, ir::Call::PureIntrinsic);
}

Expr trunc(Expr x) {
  using ir::FloatImm;
  const FloatImm* fx = x.as<FloatImm>();
  if (fx) {
    return FloatImm::make(x.type(), (fx->value < 0 ? std::ceil(fx->value) :
                                     std::floor(fx->value)));
  }
  return ir::Call::make(x.type(), "trunc", {x}, ir::Call::PureIntrinsic);
}

}  // namespace tvm
