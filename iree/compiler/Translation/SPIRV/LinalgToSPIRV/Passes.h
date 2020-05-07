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

#ifndef IREE_COMPILER_TRANSLATION_SPIRV_LINALGTOSPIRV_PASSES_H_
#define IREE_COMPILER_TRANSLATION_SPIRV_LINALGTOSPIRV_PASSES_H_

#include "mlir/Pass/Pass.h"

namespace mlir {
namespace iree_compiler {

/// Pass to tile and fuse linalg operations on buffers. The pass takes as
/// argument the `workgroupSize` that the tiling should use. Note that the
/// tile-sizes are the reverse of the workgroup size. So workgroup size along
/// "x" is used to tile the innermost loop, along "y" for the next innermost (if
/// it exists) and along "z" for the next loop (if it exists). The workgroup
/// size is expected to be of size at-most 3.
std::unique_ptr<OperationPass<FuncOp>> createLinalgTileAndFusePass(
    ArrayRef<int64_t> workGroupSize = {});

/// Pass to add the synchronizations and attributes needed to lower from PLoops
/// to GPU dialect.
std::unique_ptr<OperationPass<FuncOp>> createConvertToGPUPass();

/// Pass to perform the final conversion to SPIR-V dialect.
/// This pass converts remaining interface ops into SPIR-V global variables,
/// GPU processor ID ops into SPIR-V global variables, loop/standard ops into
/// corresponding SPIR-V ops.
std::unique_ptr<OperationPass<ModuleOp>> createConvertToSPIRVPass();

/// Pass to split computation workload to multiple sequential dispatch
/// functions. This pass operates on Linalg ops and prepares for lowering to
/// GPU, where we need to tile the workload to workgroups and workitems. If the
/// workload involves computation A and B, where B is dependent on A and A needs
/// all workgroups to complete, then we need to split A and B into different
/// kernels because there is no mechanism to perform cross-workgroup
/// synchronization within a single kernel.
std::unique_ptr<OperationPass<ModuleOp>> createSplitDispatchFunctionPass();

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_TRANSLATION_SPIRV_LINALGTOSPIRV_PASSES_H_
