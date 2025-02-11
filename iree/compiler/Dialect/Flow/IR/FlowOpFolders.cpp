// Copyright 2019 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <algorithm>
#include <numeric>

#include "iree/compiler/Dialect/Flow/IR/FlowDialect.h"
#include "iree/compiler/Dialect/Flow/IR/FlowOps.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Optional.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/StringExtras.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/IR/Value.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace Flow {

//===----------------------------------------------------------------------===//
// Streams
//===----------------------------------------------------------------------===//

namespace {

// Optimizes stream fragment arguments by:
//   - Removing any that are not used in the body
//   - Deduping arguments that refer to the same Value
struct OptimizeStreamFragmentArgs
    : public OpRewritePattern<ExStreamFragmentOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(ExStreamFragmentOp op,
                                PatternRewriter &rewriter) const override {
    if (op.body().empty()) return failure();

    bool needsMod = false;
    Block *entryBlock = &op.body().front();
    llvm::SmallVector<llvm::Optional<BlockArgument>, 8> blockArgReplacements(
        entryBlock->getNumArguments());
    assert(op.args().size() == blockArgReplacements.size());
    llvm::SmallMapVector<Value, BlockArgument, 8> argToBlockMap;
    for (auto it :
         llvm::enumerate(llvm::zip(op.args(), entryBlock->getArguments()))) {
      Value opArg = std::get<0>(it.value());
      BlockArgument blockArg = std::get<1>(it.value());
      if (blockArg.getUses().empty()) {
        // Not used - Drop.
        needsMod = true;
        blockArgReplacements[it.index()] = BlockArgument();
        continue;
      }
      auto existingIt = argToBlockMap.find(opArg);
      if (existingIt == argToBlockMap.end()) {
        // Not found - Record for deduping.
        argToBlockMap.insert(std::make_pair(opArg, blockArg));
      } else {
        // Found - Replace.
        needsMod = true;
        blockArgReplacements[it.index()] = existingIt->second;
      }
    }

    if (!needsMod) return failure();

    rewriter.startRootUpdate(op);
    llvm::SmallVector<Value, 8> newArgs;
    unsigned blockArgIndex = 0;
    for (auto it : llvm::zip(op.args(), blockArgReplacements)) {
      Value currentOpArg = std::get<0>(it);
      llvm::Optional<BlockArgument> replacement = std::get<1>(it);
      if (!replacement) {
        // No change.
        newArgs.push_back(currentOpArg);
        blockArgIndex++;
        continue;
      } else if (!replacement.getValue()) {
        // Drop.
        entryBlock->eraseArgument(blockArgIndex);
        continue;
      } else {
        // Replace.
        BlockArgument currentBlockArg = entryBlock->getArgument(blockArgIndex);
        currentBlockArg.replaceAllUsesWith(*replacement);
        entryBlock->eraseArgument(blockArgIndex);
      }
    }

    op.getOperation()->setOperands(newArgs);
    rewriter.finalizeRootUpdate(op);

    return success();
  }
};

}  // namespace

void ExStreamFragmentOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<OptimizeStreamFragmentArgs>(context);
}

//===----------------------------------------------------------------------===//
// Variables
//===----------------------------------------------------------------------===//

namespace {

/// Converts variable initializer functions that evaluate to a constant to a
/// specified initial value.
struct InlineConstVariableOpInitializer : public OpRewritePattern<VariableOp> {
  using OpRewritePattern<VariableOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(VariableOp op,
                                PatternRewriter &rewriter) const override {
    if (!op.initializer()) return failure();
    auto *symbolOp =
        SymbolTable::lookupNearestSymbolFrom(op, op.initializer().getValue());
    auto initializer = cast<FuncOp>(symbolOp);
    if (initializer.getBlocks().size() == 1 &&
        initializer.getBlocks().front().getOperations().size() == 2 &&
        isa<mlir::ReturnOp>(
            initializer.getBlocks().front().getOperations().back())) {
      auto &primaryOp = initializer.getBlocks().front().getOperations().front();
      Attribute constResult;
      if (matchPattern(primaryOp.getResult(0), m_Constant(&constResult))) {
        rewriter.replaceOpWithNewOp<VariableOp>(
            op, op.sym_name(), op.is_mutable(), op.type(), constResult);
        return success();
      }
    }
    return failure();
  }
};

}  // namespace

void VariableOp::getCanonicalizationPatterns(OwningRewritePatternList &results,
                                             MLIRContext *context) {
  results.insert<InlineConstVariableOpInitializer>(context);
}

OpFoldResult VariableLoadOp::fold(ArrayRef<Attribute> operands) {
  auto variableOp = dyn_cast_or_null<VariableOp>(
      SymbolTable::lookupNearestSymbolFrom(*this, variable()));
  if (!variableOp) return {};
  if (variableOp.is_mutable()) {
    // We can't inline mutable variables as they may be changed at any time.
    // There may still be other folders/canonicalizers that can help (such as
    // store-forwarding).
    return {};
  } else if (!variableOp.initial_value()) {
    // Uninitialized variables (or those with initializers) can't be folded as
    // we don't yet know the value. InlineConstVariableOpInitializer may help.
    return {};
  }
  return variableOp.initial_value().getValue();
}

namespace {

class PropagateVariableLoadAddress
    : public OpRewritePattern<VariableLoadIndirectOp> {
  using OpRewritePattern::OpRewritePattern;

 public:
  LogicalResult matchAndRewrite(VariableLoadIndirectOp op,
                                PatternRewriter &rewriter) const override {
    if (auto addressOp = dyn_cast_or_null<VariableAddressOp>(
            op.variable().getDefiningOp())) {
      rewriter.replaceOpWithNewOp<VariableLoadOp>(op, op.result().getType(),
                                                  addressOp.variable());
      return success();
    }
    return failure();
  }
};

}  // namespace

void VariableLoadIndirectOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<PropagateVariableLoadAddress>(context);
}

namespace {

/// Erases flow.variable.store ops that are no-ops.
/// This can happen if there was a variable load, some DCE'd usage, and a
/// store back to the same variable: we want to be able to elide the entire load
/// and store.
struct EraseUnusedVariableStoreOp : public OpRewritePattern<VariableStoreOp> {
  using OpRewritePattern<VariableStoreOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(VariableStoreOp op,
                                PatternRewriter &rewriter) const override {
    if (auto loadOp =
            dyn_cast_or_null<VariableLoadOp>(op.value().getDefiningOp())) {
      if (loadOp.variable() == op.variable()) {
        rewriter.eraseOp(op);
        return success();
      }
    }
    return failure();
  }
};

}  // namespace

void VariableStoreOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<EraseUnusedVariableStoreOp>(context);
}

namespace {

class PropagateVariableStoreAddress
    : public OpRewritePattern<VariableStoreIndirectOp> {
  using OpRewritePattern::OpRewritePattern;

 public:
  LogicalResult matchAndRewrite(VariableStoreIndirectOp op,
                                PatternRewriter &rewriter) const override {
    if (auto addressOp = dyn_cast_or_null<VariableAddressOp>(
            op.variable().getDefiningOp())) {
      rewriter.replaceOpWithNewOp<VariableStoreOp>(op, op.value(),
                                                   addressOp.variable());
      return success();
    }
    return failure();
  }
};

}  // namespace

void VariableStoreIndirectOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<PropagateVariableStoreAddress>(context);
}

//===----------------------------------------------------------------------===//
// Tensor ops
//===----------------------------------------------------------------------===//

/// Reduces the provided multidimensional index into a flattended 1D row-major
/// index. The |type| is expected to be statically shaped (as all constants
/// are).
static uint64_t getFlattenedIndex(ShapedType type, ArrayRef<uint64_t> index) {
  assert(type.hasStaticShape() && "for use on statically shaped types only");
  auto rank = type.getRank();
  auto shape = type.getShape();
  uint64_t valueIndex = 0;
  uint64_t dimMultiplier = 1;
  for (int i = rank - 1; i >= 0; --i) {
    valueIndex += index[i] * dimMultiplier;
    dimMultiplier *= shape[i];
  }
  return valueIndex;
}

OpFoldResult TensorReshapeOp::fold(ArrayRef<Attribute> operands) {
  auto sourceType = source().getType().cast<ShapedType>();
  auto resultType = result().getType().cast<ShapedType>();
  if (sourceType.hasStaticShape() && sourceType == resultType) {
    // No-op.
    return source();
  }

  // Skip intermediate reshapes.
  if (auto definingOp =
          dyn_cast_or_null<TensorReshapeOp>(source().getDefiningOp())) {
    setOperand(definingOp.getOperand());
    return result();
  }

  return {};
}

OpFoldResult TensorLoadOp::fold(ArrayRef<Attribute> operands) {
  if (auto source = operands[0].dyn_cast_or_null<ElementsAttr>()) {
    // Load directly from the constant source tensor.
    auto indices = operands.drop_front();
    if (llvm::count(indices, nullptr) == 0) {
      return source.getValue(
          llvm::to_vector<4>(llvm::map_range(indices, [](Attribute value) {
            return value.cast<IntegerAttr>().getValue().getZExtValue();
          })));
    }
  }
  return {};
}

OpFoldResult TensorStoreOp::fold(ArrayRef<Attribute> operands) {
  if (!operands[0]) return {};
  auto &value = operands[0];
  if (auto target = operands[1].dyn_cast_or_null<ElementsAttr>()) {
    // Store into the constant target tensor.
    if (target.getType().getRank() == 0) {
      return DenseElementsAttr::get(target.getType(), {value});
    }
    auto indices = operands.drop_front(2);
    if (llvm::count(indices, nullptr) == 0) {
      uint64_t offset = getFlattenedIndex(
          target.getType(),
          llvm::to_vector<4>(llvm::map_range(indices, [](Attribute value) {
            return value.cast<IntegerAttr>().getValue().getZExtValue();
          })));
      SmallVector<Attribute, 16> newContents(target.getValues<Attribute>());
      newContents[offset] = value;
      return DenseElementsAttr::get(target.getType(), newContents);
    }
  }
  return {};
}

OpFoldResult TensorSplatOp::fold(ArrayRef<Attribute> operands) {
  // TODO(benvanik): only fold when shape is constant.
  if (operands[0]) {
    // Splat value is constant and we can fold the operation.
    return SplatElementsAttr::get(result().getType().cast<ShapedType>(),
                                  operands[0]);
  }
  return {};
}

OpFoldResult TensorCloneOp::fold(ArrayRef<Attribute> operands) {
  if (operands[0]) {
    return operands[0];
  }
  // TODO(benvanik): fold if clone device placements differ.
  return operand();
}

OpFoldResult TensorSliceOp::fold(ArrayRef<Attribute> operands) {
  if (operands[0] && operands[1] && operands[2]) {
    // Fully constant arguments so we can perform the slice here.
    // TODO(benvanik): constant slice.
    return {};
  }
  return {};
}

static ElementsAttr tensorUpdate(ElementsAttr update, ElementsAttr target,
                                 ArrayRef<Attribute> startIndicesAttrs) {
  // TODO(benvanik): tensor update constant folding.
  return {};
}

OpFoldResult TensorUpdateOp::fold(ArrayRef<Attribute> operands) {
  auto indices = operands.drop_front(2);
  bool allIndicesConstant = llvm::count(indices, nullptr) == 0;
  if (operands[0] && operands[1] && allIndicesConstant) {
    // Fully constant arguments so we can perform the update here.
    return tensorUpdate(operands[0].cast<ElementsAttr>(),
                        operands[1].cast<ElementsAttr>(), indices);
  } else {
    // Replace the entire tensor when the sizes match.
    auto updateType = update().getType().cast<ShapedType>();
    auto targetType = target().getType().cast<ShapedType>();
    if (updateType.hasStaticShape() && targetType.hasStaticShape() &&
        updateType == targetType) {
      return update();
    }
  }
  return {};
}

}  // namespace Flow
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
