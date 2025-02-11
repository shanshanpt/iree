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

#include "iree/hal/vmla/vmla_executable.h"

#include "iree/base/api_util.h"
#include "iree/base/tracing.h"
#include "iree/hal/vmla/vmla_module.h"
#include "iree/schemas/vmla_executable_def_generated.h"
#include "iree/vm/bytecode_module.h"
#include "iree/vm/module.h"
#include "iree/vm/variant_list.h"

namespace iree {
namespace hal {
namespace vmla {

// static
StatusOr<ref_ptr<VMLAExecutable>> VMLAExecutable::Load(
    iree_vm_instance_t* instance, iree_vm_module_t* vmla_module,
    ExecutableSpec spec, bool allow_aliasing_data) {
  // Allocate the executable now.
  // We do this here so that if we need to clone the data we are passing that
  // to the VM loader instead of the data we may not have access to later.
  auto executable = make_ref<VMLAExecutable>(spec, allow_aliasing_data);
  RETURN_IF_ERROR(executable->Initialize(instance, vmla_module));
  return executable;
}

VMLAExecutable::VMLAExecutable(ExecutableSpec spec, bool allow_aliasing_data)
    : spec_(spec) {
  if (!allow_aliasing_data) {
    // Clone data.
    cloned_executable_data_ = {spec.executable_data.begin(),
                               spec.executable_data.end()};
    spec_.executable_data = absl::MakeConstSpan(cloned_executable_data_);
  }
}

VMLAExecutable::~VMLAExecutable() {
  IREE_TRACE_SCOPE0("VMLAExecutable::dtor");
  iree_vm_variant_list_free(interface_inputs_);
  iree_vm_context_release(context_);
  context_ = nullptr;
}

Status VMLAExecutable::Initialize(iree_vm_instance_t* instance,
                                  iree_vm_module_t* vmla_module) {
  IREE_TRACE_SCOPE0("VMLAExecutable::Initialize");

  if (spec_.executable_data.size() < 16) {
    return InvalidArgumentErrorBuilder(IREE_LOC)
           << "Flatbuffer data is not present or less than 16 bytes";
  } else if (!iree::VMLAExecutableDefBufferHasIdentifier(
                 spec_.executable_data.data())) {
    return InvalidArgumentErrorBuilder(IREE_LOC)
           << "Flatbuffer data does not have bytecode module identifier";
  }

  const auto* executable_def = ::flatbuffers::GetRoot<iree::VMLAExecutableDef>(
      spec_.executable_data.data());
  if (!executable_def || !executable_def->bytecode_module()) {
    return InvalidArgumentErrorBuilder(IREE_LOC)
           << "Failed getting root from flatbuffer data";
  }

  // Load bytecode module from the executable spec.
  iree_vm_module_t* bytecode_module = nullptr;
  RETURN_IF_ERROR(FromApiStatus(
      iree_vm_bytecode_module_create(
          iree_const_byte_span_t{reinterpret_cast<const uint8_t*>(
                                     executable_def->bytecode_module()->data()),
                                 executable_def->bytecode_module()->size()},
          IREE_ALLOCATOR_NULL, IREE_ALLOCATOR_SYSTEM, &bytecode_module),
      IREE_LOC))
      << "Failed to load executable bytecode module";

  entry_functions_.resize(
      iree_vm_module_signature(bytecode_module).export_function_count);
  for (int i = 0; i < entry_functions_.size(); ++i) {
    RETURN_IF_ERROR(
        FromApiStatus(iree_vm_module_lookup_function_by_ordinal(
                          bytecode_module, IREE_VM_FUNCTION_LINKAGE_EXPORT, i,
                          &entry_functions_[i], nullptr),
                      IREE_LOC));
  }

  // Create context and initialize shared state. Note that each executable here
  // has its own context (and thus its own vmla.interface instance).
  std::array<iree_vm_module_t*, 2> modules = {vmla_module, bytecode_module};
  auto result = FromApiStatus(iree_vm_context_create_with_modules(
                                  instance, modules.data(), modules.size(),
                                  IREE_ALLOCATOR_SYSTEM, &context_),
                              IREE_LOC)
                << "Failed resolving imports for executable module";
  iree_vm_module_release(bytecode_module);
  RETURN_IF_ERROR(result);

  // Query the Interface block we'll use to set bindings during invocation.
  iree_vm_module_state_t* module_state = nullptr;
  RETURN_IF_ERROR(FromApiStatus(iree_vm_context_resolve_module_state(
                                    context(), vmla_module, &module_state),
                                IREE_LOC));
  interface_ = ModuleStateInterface(module_state);

  // Preallocate the variant list we'll use to pass the interface into
  // executables. This makes dispatches zero-allocation (well, on the outside
  // anyway!).
  RETURN_IF_ERROR(FromApiStatus(
      iree_vm_variant_list_alloc(1, IREE_ALLOCATOR_SYSTEM, &interface_inputs_),
      IREE_LOC));
  auto interface_ref = Interface_retain_ref(interface_);
  RETURN_IF_ERROR(FromApiStatus(
      iree_vm_variant_list_append_ref_move(interface_inputs_, &interface_ref),
      IREE_LOC));

  return OkStatus();
}

}  // namespace vmla
}  // namespace hal
}  // namespace iree
