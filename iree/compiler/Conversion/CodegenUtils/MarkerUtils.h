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

//===- MarkerUtils.h - Methods for manipulating markers on Linalg op ------===//
//
// Method that set markers on Linalg operations that determine which processor
// heirarchy to use for partitioning
//
//===----------------------------------------------------------------------===//

#ifndef IREE_COMPILER_TRANSLATION_SPIRV_LINALGTOSPIRV_MARKERUTILS_H_
#define IREE_COMPILER_TRANSLATION_SPIRV_LINALGTOSPIRV_MARKERUTILS_H_

#include "llvm/ADT/StringRef.h"
#include "mlir/Support/LLVM.h"

namespace mlir {

class Operation;
namespace iree_compiler {

/// Marker to denote that do not tile the linalg operation.
StringRef getNoTileMarker();

/// Marker to denote that a linalg operation is to be partitioned to workitems.
StringRef getWorkItemMarker();

/// Returns true if an operation has any marker.
bool hasMarker(Operation *);

/// Returns true if an operation has marker to denote that it is not to be
/// tiled.
bool hasNoTileMarker(Operation *);

/// Returns true if an operation has marker to denote that it is to be
/// partitioned to workitems.
bool hasWorkItemMarker(Operation *);

/// Sets marker to prevent tiling of a linalg operation.
void setNoTileMarker(Operation *);

/// Sets marker to denote that a linalg operation is to be partitioned to
/// workitems.
void setWorkItemMarker(Operation *);

}  // namespace iree_compiler
}  // namespace mlir

#endif  // IREE_COMPILER_TRANSLATION_SPIRV_LINALGTOSPIRV_MARKERUTILS_H_
