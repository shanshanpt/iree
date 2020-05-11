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

// This files defines a helper to trigger the registration of dialects to
// the system.
//
// Based on MLIR's InitAllDialects but without dialects we don't care about.

#ifndef IREE_TOOLS_INIT_DIALECTS_H_
#define IREE_TOOLS_INIT_DIALECTS_H_

#include "iree/compiler/Dialect/Flow/IR/FlowDialect.h"
#include "iree/compiler/Dialect/HAL/IR/HALDialect.h"
#include "iree/compiler/Dialect/IREE/IR/IREEDialect.h"
#include "iree/compiler/Dialect/Shape/IR/ShapeDialect.h"
#include "iree/compiler/Dialect/VM/IR/VMDialect.h"
#include "iree/compiler/Dialect/VMLA/IR/VMLADialect.h"
#include "iree/compiler/Dialect/Vulkan/IR/VulkanDialect.h"
#include "mlir/Dialect/Affine/IR/AffineOps.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/LLVMIR/LLVMDialect.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/Quant/QuantOps.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/SDBM/SDBMDialect.h"
#include "mlir/Dialect/SPIRV/SPIRVDialect.h"
#include "mlir/Dialect/Shape/IR/Shape.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Vector/VectorOps.h"
#include "mlir/IR/Dialect.h"

namespace mlir {

// This function should be called before creating any MLIRContext if one expect
// all the possible dialects to be made available to the context automatically.
inline void registerMlirDialects() {
  static bool init_once = []() {
    registerDialect<AffineDialect>();
    registerDialect<gpu::GPUDialect>();
    registerDialect<LLVM::LLVMDialect>();
    registerDialect<linalg::LinalgDialect>();
    registerDialect<scf::SCFDialect>();
    registerDialect<quant::QuantizationDialect>();
    registerDialect<shape::ShapeDialect>();
    registerDialect<spirv::SPIRVDialect>();
    registerDialect<StandardOpsDialect>();
    registerDialect<vector::VectorDialect>();
    registerDialect<SDBMDialect>();
    return true;
  }();
  (void)init_once;
}
}  // namespace mlir

namespace mlir {
namespace iree_compiler {

// This function should be called before creating any MLIRContext if one expect
// all the possible dialects to be made available to the context automatically.
inline void registerIreeDialects() {
  static bool init_once = []() {
    registerDialect<IREE::Flow::FlowDialect>();
    registerDialect<IREE::HAL::HALDialect>();
    registerDialect<ShapeDialect>();
    registerDialect<IREEDialect>();
    registerDialect<IREE::VM::VMDialect>();
    registerDialect<IREE::VMLA::VMLADialect>();
    registerDialect<IREE::Vulkan::VulkanDialect>();
    return true;
  }();
  (void)init_once;
}
}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_TOOLS_INIT_DIALECTS_H_
