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

//===- LowerToSPIRV.cpp - Lower from XLA to Linalg to SPIR-V dialect-------===//
//
// Implementation of conversion from XLA-HLO to Linalg to SPIR-V dialect.
//
//===----------------------------------------------------------------------===//

#include "iree/compiler/Translation/SPIRV/LinalgToSPIRV/LowerToSPIRV.h"

#include "iree/compiler/Dialect/Shape/Transforms/Passes.h"
#include "iree/compiler/Translation/CodegenPasses/Passes.h"
#include "iree/compiler/Translation/SPIRV/LinalgToSPIRV/Passes.h"
#include "mlir/Conversion/GPUToSPIRV/ConvertGPUToSPIRV.h"
#include "mlir/Conversion/LoopsToGPU/LoopsToGPUPass.h"
#include "mlir/Conversion/StandardToSPIRV/ConvertStandardToSPIRV.h"
#include "mlir/Conversion/StandardToSPIRV/ConvertStandardToSPIRVPass.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/GPU/Passes.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/SPIRV/Passes.h"
#include "mlir/Dialect/SPIRV/SPIRVLowering.h"
#include "mlir/Dialect/SPIRV/SPIRVOps.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Module.h"
#include "mlir/IR/StandardTypes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/FoldUtils.h"
#include "mlir/Transforms/Passes.h"
#include "tensorflow/compiler/mlir/xla/transforms/passes.h"

namespace mlir {
namespace iree_compiler {

namespace {
/// These options are only for testing purposes. For actual execution with IREE,
/// these are computed by IREE/Backends automatically.
struct WorkGroupOptions : public PassPipelineOptions<WorkGroupOptions> {
  ListOption<int64_t> workGroupSize{
      *this, "workgroup-size",
      llvm::cl::desc(
          "Number of workgroups to dispatch for the SPIR-V module; at most "
          "three integers standarding for the x, y, and z dimension; "
          "additional arguments will be ignored (used only for testing)"),
      llvm::cl::ZeroOrMore, llvm::cl::MiscFlags::CommaSeparated};
};

void addLinalgToSPIRVPasses(OpPassManager &pm,
                            ArrayRef<int64_t> workGroupSize) {
  //===--------------------------------------------------------------------===//
  // Initial clean up.
  //===--------------------------------------------------------------------===//
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());

  //===--------------------------------------------------------------------===//
  // Tile Linalg on buffers.
  //
  // Pre-conditions:
  //   - All Linalg ops have buffer semantics.
  //
  // Post-conditions:
  //   - loop.parallel ops are generated for mapping to workgroups.
  //   - Linalg ops are nested inside loop.parallel ops and ready for mapping
  //     to workitems.
  //===--------------------------------------------------------------------===//
  pm.addPass(createLinalgTileAndFusePass(workGroupSize));

  //===--------------------------------------------------------------------===//
  // Map to GPU processor IDs.
  //
  // Post-conditions:
  //   - loop.parallel ops are converted to loop.for ops and mapped to
  //     workgroups.
  //   - Linalg ops are converted to loop.for ops and mapped to workitems.
  //===--------------------------------------------------------------------===//
  pm.addPass(createConvertToGPUPass());
  pm.addPass(createLowerAffinePass());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());

  //===--------------------------------------------------------------------===//
  // Resolve shape related ops.
  //
  // Pre-conditions:
  //   - All dynamic tensors bridge through a shapex.tie_shape op with the
  //     appropriate shape.
  //   - No shapex.get_ranked_shape ops exist.
  //   - Shape folding and canonicalization has been done.
  // Post-conditions:
  //   - shapex.tie_shape and other shapex ops are all converted away.
  //   - std.dim ops are traced back and replaced by the corresponding
  //     hal.inteface.load.constant op. There are no std.dim ops left
  //     in the IR.
  //===--------------------------------------------------------------------===//
  pm.addPass(createResolveShapeOpsPass());

  //===--------------------------------------------------------------------===//
  // Prepare stdandard ops for SPIR-V conversion.
  //
  // Post-conditions:
  //   - Load/store on std.subview ops are converted into load/store on the
  //     original buffers.
  //===--------------------------------------------------------------------===//
  pm.addPass(createLegalizeStdOpsForSPIRVLoweringPass());
  pm.addPass(createCanonicalizerPass());
  pm.addPass(createCSEPass());

  //===--------------------------------------------------------------------===//
  // Final conversion to SPIR-V dialect.
  //
  // Post-conditions:
  //   - All ops are converted to SPIR-V counterparts.
  //   - spv.module ops are formed to hold all SPIR-V ops.
  //===--------------------------------------------------------------------===//
  pm.addPass(createConvertToSPIRVPass());

  //===--------------------------------------------------------------------===//
  // SPIR-V dialect level conversions.
  //
  // Post-conditions:
  //   - SPIR-V Entry point ops are inserted.
  //   - Required version/extension/capability are deduced.
  //===--------------------------------------------------------------------===//
  OpPassManager &spirvModulePM = pm.nest<spirv::ModuleOp>();
  spirvModulePM.addPass(spirv::createLowerABIAttributesPass());
  spirvModulePM.addPass(createCanonicalizerPass());
  spirvModulePM.addPass(createCSEPass());
  spirvModulePM.addPass(spirv::createUpdateVersionCapabilityExtensionPass());
}
}  // namespace

void buildSPIRVTransformPassPipeline(OpPassManager &pm,
                                     ArrayRef<int64_t> workGroupSize) {
  //===--------------------------------------------------------------------===//
  // Inline the impl dispatch function into the wrapper dispatch function.
  //
  // TODO(antiagainst): re-evaluate the inlining timing.
  //===--------------------------------------------------------------------===//
  pm.addPass(createInlinerPass());

  //===--------------------------------------------------------------------===//
  // Inject shape calculation for output buffers.
  //
  // Pre-conditions:
  //   - All transformations altering the tensor-level shapes have been done.
  //   - "Root" dynamic tensors all pass through a single shapex.tie_shape
  //     use which associates them to their shape.
  //   - Loose, non-associated shapex.get_ranked_shape ops can exist anywhere
  //     and will be resolved.
  // Post-conditions:
  //   - All dynamic tensors bridge through a shapex.tie_shape op with the
  //     appropriate shape.
  //   - No shapex.get_ranked_shape ops exist.
  //   - Shape folding and canonicalization has been done.
  //===--------------------------------------------------------------------===//
  pm.addNestedPass<FuncOp>(Shape::createTieDynamicShapesPass());
  pm.addNestedPass<FuncOp>(Shape::createMaterializeShapeCalculationsPass());
  pm.addNestedPass<FuncOp>(Shape::createHoistShapeCalculationsPass());

  //===--------------------------------------------------------------------===//
  // Convert XLA HLO ops to Linalg ops with buffer semantics.
  //
  // Post-conditions:
  //   - All XLA HLO ops are converted.
  //   - All Linalg ops are operating on buffers.
  //===--------------------------------------------------------------------===//
  pm.addPass(createDecomposeHLOClampPass());
  addHLOToLinalgOnBuffersPasses(pm);

  //===--------------------------------------------------------------------===//
  // Convert Linalg ops to SPIR-V ops.
  //
  // Post-conditions:
  //   - All Linalg/Loops/GPU/Affine/Standard ops are converted away.
  //   - The module contains the final spv.module ready for serialization.
  //===--------------------------------------------------------------------===//
  addLinalgToSPIRVPasses(pm, workGroupSize);
}

static PassPipelineRegistration<WorkGroupOptions> linalgToSPIRVPipeline(
    "iree-codegen-linalg-to-spirv-pipeline",
    "Runs the progressive lowering pipeline from Linalg to SPIR-V",
    [](OpPassManager &passManager, const WorkGroupOptions &options) {
      SmallVector<int64_t, 2> workGroupSize;
      workGroupSize.assign(options.workGroupSize.begin(),
                           options.workGroupSize.end());
      addLinalgToSPIRVPasses(passManager, workGroupSize);
    });

static PassPipelineRegistration<WorkGroupOptions> hloToLinalgSPIRVPipeline(
    "iree-codegen-hlo-to-spirv-pipeline",
    "Runs the progressive lowering pipeline from XLA HLO to Linalg to SPIR-V",
    [](OpPassManager &passManager, const WorkGroupOptions &options) {
      SmallVector<int64_t, 2> workGroupSize;
      workGroupSize.assign(options.workGroupSize.begin(),
                           options.workGroupSize.end());
      buildSPIRVTransformPassPipeline(passManager, workGroupSize);
    });

}  // namespace iree_compiler
}  // namespace mlir
