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

#ifndef IREE_VM_STACK_H_
#define IREE_VM_STACK_H_

#include <stddef.h>
#include <stdint.h>

#include "iree/base/alignment.h"
#include "iree/base/api.h"
#include "iree/vm/module.h"
#include "iree/vm/ref.h"
#include "iree/vm/variant_list.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Maximum stack depth, in frames.
#define IREE_MAX_STACK_DEPTH 32

// Maximum register count per bank.
// This determines the bits required to reference registers in the VM bytecode.
#define IREE_I32_REGISTER_COUNT 0x7FFF
#define IREE_REF_REGISTER_COUNT 0x7FFF

#define IREE_I32_REGISTER_MASK 0x7FFF

#define IREE_REF_REGISTER_TYPE_BIT 0x8000
#define IREE_REF_REGISTER_MOVE_BIT 0x4000
#define IREE_REF_REGISTER_MASK 0x3FFF

// Pointers to typed register storage.
typedef struct {
  // 16-byte aligned i32 register array.
  int32_t* i32;
  // Ordinal mask defining which ordinal bits are valid. All i32 indexing must
  // be ANDed with this mask.
  uint16_t i32_mask;
  // Naturally aligned ref register array.
  iree_vm_ref_t* ref;
  // Ordinal mask defining which ordinal bits are valid. All ref indexing must
  // be ANDed with this mask.
  uint16_t ref_mask;
} iree_vm_registers_t;

// A single stack frame within the VM.
typedef struct iree_vm_stack_frame {
  // NOTE: to get better cache hit rates we put the most frequently accessed
  // members first.

  // Current program counter within the function.
  // Implementations may treat this offset differently, treating it as a byte
  // offset (such as in the case of VM bytecode), a block identifier (compiled
  // code), etc.
  iree_vm_source_offset_t pc;

  // Base offsets of register arrays into storage.
  // NOTE: these are not valid host pointers and are instead aligned byte
  // offsets into the iree_vm_stack_t register_storage buffer. In order to get
  // the full host pointer these must be added to the register_storage pointer.
  iree_vm_registers_t register_base;

  // Function that the stack frame is within.
  iree_vm_function_t function;

  // Cached module state pointer for the module containing |function|.
  // This removes the need to lookup the module state when control returns to
  // the function during continuation or from a return instruction.
  iree_vm_module_state_t* module_state;

  // Pointer to a register list where callers can source their return registers.
  // If omitted then the return values are assumed to be left-aligned in the
  // register banks.
  const iree_vm_register_list_t* return_registers;
} iree_vm_stack_frame_t;

// A state resolver that can allocate or lookup module state.
typedef struct iree_vm_state_resolver {
  void* self;
  iree_status_t(IREE_API_PTR* query_module_state)(
      void* state_resolver, iree_vm_module_t* module,
      iree_vm_module_state_t** out_module_state);
} iree_vm_state_resolver_t;

// A fiber stack used for storing stack frame state during execution.
// All required state is stored within the stack and no host thread-local state
// is used allowing us to execute multiple fibers on the same host thread.
typedef struct iree_vm_stack {
  // NOTE: to get better cache hit rates we put the most frequently accessed
  // members first.

  // Base pointer to a register storage buffer.
  // Each stack frame has an offset into this buffer that is combined to get the
  // storage pointer; this indirection allows for the register storage to be
  // dynamically reallocated during execution in case it needs to grow.
  iree_host_size_t register_storage_capacity;
  iree_host_size_t register_storage_size;
  void* register_storage;

  // Depth of the stack, in frames. 0 indicates an empty stack.
  int32_t depth;
  // [0-depth) valid stack frames.
  iree_vm_stack_frame_t frames[IREE_MAX_STACK_DEPTH];

  // Allocator used for dynamic stack allocations.
  iree_allocator_t allocator;

  // Resolves a module to a module state within a context.
  // This will be called on function entry whenever module transitions occur.
  iree_vm_state_resolver_t state_resolver;
} iree_vm_stack_t;

// Constructs a stack in-place in |out_stack|.
IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_vm_stack_init(iree_vm_state_resolver_t state_resolver,
                   iree_allocator_t allocator, iree_vm_stack_t* out_stack);

// Destructs |stack|.
IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_vm_stack_deinit(iree_vm_stack_t* stack);

// Returns the current stack frame or nullptr if the stack is empty.
IREE_API_EXPORT iree_vm_stack_frame_t* IREE_API_CALL
iree_vm_stack_current_frame(iree_vm_stack_t* stack);

// Returns the parent stack frame or nullptr if the stack is empty.
IREE_API_EXPORT iree_vm_stack_frame_t* IREE_API_CALL
iree_vm_stack_parent_frame(iree_vm_stack_t* stack);

// Returns pointers to the stack frame register storage.
// Note that the pointers may be invalidated on function entry and must be
// requeried if any stack operations are performed.
IREE_API_EXPORT iree_status_t IREE_API_CALL iree_vm_stack_frame_registers(
    iree_vm_stack_t* stack, iree_vm_stack_frame_t* stack_frame,
    iree_vm_registers_t* out_registers);

// Enters into the given |function| and returns the callee stack frame.
// May invalidate any pointers into stack frame registers.
IREE_API_EXPORT iree_status_t IREE_API_CALL iree_vm_stack_function_enter(
    iree_vm_stack_t* stack, iree_vm_function_t function,
    const iree_vm_register_list_t* argument_registers,
    iree_vm_stack_frame_t** out_callee_frame,
    iree_vm_registers_t* out_callee_registers);

// Leaves the current stack frame.
IREE_API_EXPORT iree_status_t IREE_API_CALL iree_vm_stack_function_leave(
    iree_vm_stack_t* stack, const iree_vm_register_list_t* result_registers,
    iree_vm_stack_frame_t** out_caller_frame,
    iree_vm_registers_t* out_caller_registers);

// Enters into a `[external]` marshaling wrapper and populates the stack frame
// with the given |arguments|. External frames have no matching function and
// will be displayed in tools as opaque entries.
//
// Callers must provide an allocated |out_argument_registers| list with enough
// storage for all of the |arguments|. Upon return the list will contain the
// ordinals of the arguments that can be passed to iree_vm_stack_function_enter.
//
// External frame registers will be populated with the given |arguments| in ABI
// order (0-to-N for each register type). Future callees will take consume the
// argument registers by move from the |out_argument_registers| list.
IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_vm_stack_function_enter_external(
    iree_vm_stack_t* stack, iree_vm_variant_list_t* arguments,
    iree_vm_register_list_t* out_argument_registers);

// Leaves an `[external]` marshaling wrapper and populates the |results| with
// the function result registers.
IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_vm_stack_function_leave_external(iree_vm_stack_t* stack,
                                      iree_vm_variant_list_t* results);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_VM_STACK_H_
