// Copyright 2020 Google LLC
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

//===- XLAToLinalgOnTensors.cpp - Pass to convert XLA to Linalg on tensors-===//
//
// Pass to convert from XLA to linalg on tensers. Uses the patterns from
// tensorflow/compiler/mlir/xla/transforms/xla_legalize_to_linalg.cc along with
// some IREE specific patterns.
//
//===----------------------------------------------------------------------===//
#include <memory>

#include "iree/compiler/Conversion/HLOToLinalg/Passes.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Function.h"
#include "mlir/IR/Location.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Operation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/Passes.h"
#include "tensorflow/compiler/mlir/xla/ir/hlo_ops.h"
#include "tensorflow/compiler/mlir/xla/transforms/rewriters.h"

namespace mlir {
namespace iree_compiler {
namespace {

// These are duplicated from xla/transforms/xla_legalize_to_linalg.cc.
ArrayAttr getNParallelLoopsAttrs(unsigned nParallelLoops, Builder& b) {
  auto parallelLoopTypeAttr = b.getStringAttr("parallel");
  SmallVector<Attribute, 3> iteratorTypes(nParallelLoops, parallelLoopTypeAttr);
  return b.getArrayAttr(iteratorTypes);
}

ShapedType getXLAOpResultType(Operation* op) {
  return op->getResult(0).getType().cast<ShapedType>();
}

template <bool isLHLO = true>
bool verifyXLAOpTensorSemantics(Operation* op) {
  auto verifyType = [&](Value val) -> bool {
    return (val.getType().isa<RankedTensorType>());
  };
  return llvm::all_of(op->getOperands(), verifyType) &&
         llvm::all_of(op->getResults(), verifyType);
}

/// Conversion pattern for splat constants that are not zero-dim tensors, i.e
/// constant dense<...> : tensor<?xelem-type> -> linalg.generic op.
class SplatConstConverter : public OpConversionPattern<ConstantOp> {
 public:
  using OpConversionPattern<ConstantOp>::OpConversionPattern;

  LogicalResult matchAndRewrite(
      ConstantOp op, ArrayRef<Value> args,
      ConversionPatternRewriter& rewriter) const final {
    if (!verifyXLAOpTensorSemantics(op)) {
      return failure();
    }
    auto resultType = getXLAOpResultType(op);
    if (resultType.getRank() == 0) return failure();
    auto valueAttr = op.value().template cast<DenseElementsAttr>();
    if (!valueAttr.isSplat()) return failure();

    OpBuilder::InsertionGuard linalgOpGuard(rewriter);
    auto nloops = std::max<unsigned>(resultType.getRank(), 1);
    auto loc = op.getLoc();

    auto linalgOp = rewriter.create<linalg::GenericOp>(
        loc, resultType, args, rewriter.getI64IntegerAttr(0),
        rewriter.getI64IntegerAttr(1),
        rewriter.getAffineMapArrayAttr(rewriter.getMultiDimIdentityMap(nloops)),
        getNParallelLoopsAttrs(nloops, rewriter),
        /*doc=*/nullptr,
        /*library_call=*/nullptr);
    auto* region = &linalgOp.region();
    auto* block = rewriter.createBlock(region, region->end());
    rewriter.setInsertionPointToEnd(block);
    auto stdConstOp =
        rewriter.create<mlir::ConstantOp>(loc, valueAttr.getSplatValue());
    rewriter.create<linalg::YieldOp>(loc, stdConstOp.getResult());
    rewriter.replaceOp(op, linalgOp.getResults());
    return success();
  }
};

struct ConvertHLOToLinalgOnTensorsPass
    : public PassWrapper<ConvertHLOToLinalgOnTensorsPass, FunctionPass> {
  void runOnFunction() override {
    OwningRewritePatternList patterns;
    populateHLOToLinalgOnTensorsConversionPatterns(&getContext(), patterns);

    ConversionTarget target(getContext());
    // Allow constant to appear in Linalg op regions.
    target.addDynamicallyLegalOp<ConstantOp>([](ConstantOp op) -> bool {
      return isa<linalg::LinalgOp>(op.getOperation()->getParentOp());
    });
    // Don't convert the body of reduction ops.
    target.addDynamicallyLegalDialect<xla_hlo::XlaHloDialect>(
        Optional<ConversionTarget::DynamicLegalityCallbackFn>(
            [](Operation* op) {
              auto parentOp = op->getParentRegion()->getParentOp();
              return isa<xla_hlo::ReduceOp>(parentOp) ||
                     isa<xla_hlo::ReduceWindowOp>(parentOp);
            }));
    // Let the rest fall through.
    target.markUnknownOpDynamicallyLegal([](Operation*) { return true; });

    if (failed(applyPartialConversion(getFunction(), target, patterns))) {
      signalPassFailure();
    }
  }
};

}  // namespace

void populateHLOToLinalgOnTensorsConversionPatterns(
    MLIRContext* context, OwningRewritePatternList& patterns) {
  xla_hlo::populateHLOToLinalgConversionPattern(context, &patterns);
  patterns.insert<SplatConstConverter>(context);
}

std::unique_ptr<OperationPass<FuncOp>> createHLOToLinalgOnTensorsPass() {
  return std::make_unique<ConvertHLOToLinalgOnTensorsPass>();
}

static PassRegistration<ConvertHLOToLinalgOnTensorsPass> legalize_pass(
    "iree-codegen-hlo-to-linalg-on-tensors",
    "Convert from XLA-HLO ops to Linalg ops on tensors");

}  // namespace iree_compiler
}  // namespace mlir
