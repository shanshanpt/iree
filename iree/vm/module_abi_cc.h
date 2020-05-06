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

#ifndef IREE_VM_MODULE_ABI_CC_H_
#define IREE_VM_MODULE_ABI_CC_H_

#include <cstring>
#include <memory>

#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "iree/base/api.h"
#include "iree/base/api_util.h"
#include "iree/base/status.h"
#include "iree/vm/module.h"
#include "iree/vm/module_abi_packing.h"
#include "iree/vm/stack.h"

#ifndef __cplusplus
#error "This header is meant for use with C++ module implementations."
#endif  // __cplusplus

namespace iree {
namespace vm {

// A native module as exported to the VM dynamic module linking API.
// This allows easy wrapping of C++ module implementations and removes a
// majority of the boilerplate required with marshaling args/results out/in of
// the VM via the ABI.
//
// Functions are defined on the State type as member functions returning either
// Status or StatusOr. Arguments are passed as primitive types (int32_t),
// wrapped ref objects (vm::ref<my_type_t>&), or some nesting of std::array,
// std::tuple, and absl::Span to match fixed-length arrays of the same type,
// tuples of mixed types, or dynamic arrays (variadic arguments). Results may be
// returned as either their type or an std::tuple/std::array of types.
//
// Usage:
//   // Per-context module state that must only be thread-compatible.
//   // Define
//   struct MyState final {
//     StatusOr<std::tuple<int32_t, int32_t>> MyMethod1(vm::ref<my_type_t> t);
//   };
//
//   // Table of functions mapped to their name in the IR.
//   static const vm::NativeFunction<MyState> kMyFunctions[] = {
//     vm::MakeNativeFunction("my_method_1", &MyState::MyMethod1),
//   };
//
//   // The outer module wrapper shared across contexts.
//   // Must be thread-safe.
//   struct MyModule : public NativeModule<MyState> {
//     StatusOr<std::unique_ptr<MyState>> CreateState(iree_allocator_t) {
//       // You could pass in thread-safe shared resources to MyState.
//       return std::make_unique<MyState>();
//     }
//   };
//
//   // Creates the module and exposes it as a C interface.
//   // Ownership transfers to the caller.
//   iree_vm_module_t* create_my_module(iree_allocator_t allocator) {
//     return std::make_unique<MyModule>("my_module", allocator,
//         absl::MakeConstSpan(kCustomModuleFunctions)).release()->interface();
//   }
template <typename State>
class NativeModule {
 public:
  NativeModule(const char* name, iree_allocator_t allocator,
               absl::Span<const NativeFunction<State>> dispatch_table)
      : name_(name), allocator_(allocator), dispatch_table_(dispatch_table) {
    CHECK_OK(FromApiStatus(iree_vm_module_init(&interface_, this), IREE_LOC));
    interface_.destroy = NativeModule::ModuleDestroy;
    interface_.name = NativeModule::ModuleName;
    interface_.signature = NativeModule::ModuleSignature;
    interface_.get_function = NativeModule::ModuleGetFunction;
    interface_.lookup_function = NativeModule::ModuleLookupFunction;
    interface_.alloc_state = NativeModule::ModuleAllocState;
    interface_.free_state = NativeModule::ModuleFreeState;
    interface_.resolve_import = NativeModule::ModuleResolveImport;
    interface_.call = NativeModule::ModuleCall;
  }

  virtual ~NativeModule() = default;

  // C API module interface bound to this NativeModule instance.
  iree_vm_module_t* interface() { return &interface_; }

 protected:
  // Creates a new per-context module State holder.
  virtual StatusOr<std::unique_ptr<State>> CreateState(
      iree_allocator_t allocator) = 0;

 private:
  static NativeModule* FromModulePointer(void* self) {
    return reinterpret_cast<NativeModule*>(self);
  }
  static State* FromStatePointer(void* self) {
    return reinterpret_cast<State*>(self);
  }

  static iree_status_t ModuleDestroy(void* self) {
    delete FromModulePointer(self);
    return IREE_STATUS_OK;
  }

  static iree_string_view_t ModuleName(void* self) {
    auto* module = FromModulePointer(self);
    return iree_make_cstring_view(module->name_);
  }

  static iree_vm_module_signature_t ModuleSignature(void* self) {
    auto* module = FromModulePointer(self);
    iree_vm_module_signature_t signature = {0};
    signature.import_function_count = 0;
    signature.export_function_count = module->dispatch_table_.size();
    signature.internal_function_count = 0;
    return signature;
  }

  static iree_status_t ModuleGetFunction(
      void* self, iree_vm_function_linkage_t linkage, int32_t ordinal,
      iree_vm_function_t* out_function, iree_string_view_t* out_name,
      iree_vm_function_signature_t* out_signature) {
    if (out_function) {
      std::memset(out_function, 0, sizeof(*out_function));
    }
    if (out_name) {
      out_name->data = nullptr;
      out_name->size = 0;
    }
    if (out_signature) {
      std::memset(out_signature, 0, sizeof(*out_signature));
    }
    auto* module = FromModulePointer(self);
    if (ordinal < 0 || ordinal > module->dispatch_table_.size()) {
      return IREE_STATUS_INVALID_ARGUMENT;
    }
    if (out_function) {
      out_function->module = module->interface();
      out_function->linkage = IREE_VM_FUNCTION_LINKAGE_EXPORT;
      out_function->ordinal = ordinal;
    }
    if (out_name) {
      const auto& dispatch_function = module->dispatch_table_[ordinal];
      *out_name = iree_make_cstring_view(dispatch_function.name);
    }
    return IREE_STATUS_OK;
  }

  static iree_status_t ModuleLookupFunction(void* self,
                                            iree_vm_function_linkage_t linkage,
                                            iree_string_view_t name,
                                            iree_vm_function_t* out_function) {
    if (!out_function) return IREE_STATUS_INVALID_ARGUMENT;
    std::memset(out_function, 0, sizeof(*out_function));
    if (!name.data || !name.size) return IREE_STATUS_INVALID_ARGUMENT;

    auto* module = FromModulePointer(self);
    out_function->module = module->interface();
    out_function->linkage = IREE_VM_FUNCTION_LINKAGE_EXPORT;
    for (int i = 0; i < module->dispatch_table_.size(); ++i) {
      if (iree_string_view_compare(
              name, iree_make_cstring_view(module->dispatch_table_[i].name)) ==
          0) {
        out_function->ordinal = i;
        return IREE_STATUS_OK;
      }
    }
    return IREE_STATUS_NOT_FOUND;
  }

  static iree_status_t ModuleAllocState(
      void* self, iree_allocator_t allocator,
      iree_vm_module_state_t** out_module_state) {
    if (!out_module_state) return IREE_STATUS_INVALID_ARGUMENT;
    *out_module_state = nullptr;

    auto* module = FromModulePointer(self);
    auto module_state_or = module->CreateState(allocator);
    if (!module_state_or.ok()) {
      return ToApiStatus(module_state_or.status());
    }
    auto module_state = std::move(module_state_or).value();

    *out_module_state =
        reinterpret_cast<iree_vm_module_state_t*>(module_state.release());
    return IREE_STATUS_OK;
  }

  static iree_status_t ModuleFreeState(void* self,
                                       iree_vm_module_state_t* module_state) {
    if (!module_state) return IREE_STATUS_INVALID_ARGUMENT;
    delete FromStatePointer(module_state);
    return IREE_STATUS_OK;
  }

  static iree_status_t ModuleResolveImport(void* self,
                                           iree_vm_module_state_t* module_state,
                                           int32_t ordinal,
                                           iree_vm_function_t function) {
    // C++ API does not yet support imports.
    return IREE_STATUS_FAILED_PRECONDITION;
  }

  static iree_status_t ModuleCall(
      void* self, iree_vm_stack_t* stack, iree_vm_function_t function,
      const iree_vm_register_list_t* argument_registers,
      iree_vm_execution_result_t* out_result) {
    if (!out_result) return IREE_STATUS_INVALID_ARGUMENT;
    std::memset(out_result, 0, sizeof(*out_result));
    if (!stack) return IREE_STATUS_INVALID_ARGUMENT;
    auto* module = FromModulePointer(self);
    if (function.ordinal < 0 ||
        function.ordinal > module->dispatch_table_.size()) {
      return IREE_STATUS_INVALID_ARGUMENT;
    }
    const auto& info = module->dispatch_table_[function.ordinal];
    auto* state = FromStatePointer(frame->module_state);
    auto status = info.call(info.ptr, state, stack, function,
                            argument_registers, out_result);
    if (!status.ok()) {
      status = iree::Annotate(
          status,
          absl::StrCat("while executing ", module->name_, ".", info.name));
      return ToApiStatus(status);
    }
    return IREE_STATUS_OK;
  }

  const char* name_;
  const iree_allocator_t allocator_;
  iree_vm_module_t interface_;

  const absl::Span<const NativeFunction<State>> dispatch_table_;
};

}  // namespace vm
}  // namespace iree

#endif  // IREE_VM_MODULE_ABI_CC_H_
