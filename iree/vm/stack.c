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

#include "iree/vm/stack.h"

#include <string.h>

#include "iree/base/api.h"
#include "iree/vm/module.h"

#ifndef NDEBUG
#define VMCHECK(expr) assert(expr)
#else
#define VMCHECK(expr)
#endif  // NDEBUG

// Chosen to fit quite a few i32 registers and a reasonable amount of ref
// registers (that are 2 * sizeof(void*)).
#define IREE_VM_STACK_DEFAULT_ALLOC_SIZE 16 * 1024

// Rebases |registers| from to the register storage arrays to base offsets.
static void iree_vm_registers_to_base(iree_vm_stack_t* stack,
                                      const iree_vm_registers_t registers,
                                      iree_vm_registers_t* out_base_registers) {
  out_base_registers->i32_mask = registers.i32_mask;
  out_base_registers->i32 =
      (int32_t*)((uintptr_t)registers.i32 - (uintptr_t)stack->register_storage);
  out_base_registers->ref_mask = registers.ref_mask;
  out_base_registers->ref =
      (iree_vm_ref_t*)((uintptr_t)registers.ref -
                       (uintptr_t)stack->register_storage);
}

// Rebases |base_registers| into to the register storage arrays.
static void iree_vm_registers_from_base(
    iree_vm_stack_t* stack, const iree_vm_registers_t base_registers,
    iree_vm_registers_t* out_registers) {
  out_registers->i32_mask = base_registers.i32_mask;
  out_registers->i32 = (int32_t*)((uintptr_t)stack->register_storage +
                                  (uintptr_t)base_registers.i32);
  out_registers->ref_mask = base_registers.ref_mask;
  out_registers->ref = (iree_vm_ref_t*)((uintptr_t)stack->register_storage +
                                        (uintptr_t)base_registers.ref);
}

// Allocates a new block of register storage for the given |function| frame.
// May reallocate the stack if required. |out_registers| will be set with the
// valid register masks and host pointer offsets.
static iree_status_t iree_vm_stack_reserve_register_storage(
    iree_vm_stack_t* stack, iree_vm_function_t function,
    iree_vm_registers_t* out_registers) {
  // round up register counts

  // set masks

  iree_host_size_t frame_size = 0;
  if (stack->register_storage_size + frame_size) {
    // reallocate storage
  }

  // set registers

#ifndef NDEBUG
//   memset(callee_frame->registers.i32, 0xCD,
//          sizeof(callee_frame->registers.i32));
//   memset(callee_frame->registers.ref, 0xCD,
//          sizeof(callee_frame->registers.ref));
#endif  // !NDEBUG

  return IREE_STATUS_OK;
}

static void iree_vm_stack_release_register_storage(
    iree_vm_stack_t* stack, iree_vm_stack_frame_t* frame) {
  // DO NOT SUBMIT
  // DO NOT SUBMIT release ref registers remaining
  // iree_vm_registers_t* registers = &callee_frame->registers;
  // for (int i = 0; i < registers->ref_register_count; ++i) {
  //   iree_vm_ref_release(&registers->ref[i]);
  // }
}

IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_vm_stack_init(iree_vm_state_resolver_t state_resolver,
                   iree_allocator_t allocator, iree_vm_stack_t* out_stack) {
  memset(out_stack, 0, sizeof(iree_vm_stack_t));
  out_stack->state_resolver = state_resolver;
  out_stack->allocator = allocator;

  // Default allocation size for register storage. This is a conservative
  // estimate that we use to try to avoid additional allocations later on while
  // also not overallocating for simple methods. Really we should be tuning this
  // per-platform and per-module. We could try inlining a small amount of store
  // inside of the iree_vm_stack_t itself to avoid allocations entirely but
  // that's a bit more complex.
  out_stack->register_storage_capacity = IREE_VM_STACK_DEFAULT_ALLOC_SIZE;
  out_stack->register_storage_size = 0;
  IREE_RETURN_IF_ERROR(iree_allocator_malloc(
      out_stack->allocator, out_stack->register_storage_capacity,
      &out_stack->register_storage));

  return IREE_STATUS_OK;
}

IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_vm_stack_deinit(iree_vm_stack_t* stack) {
  // Pop all stack frames to ensure that we release all held resources.
  while (stack->depth) {
    IREE_RETURN_IF_ERROR(iree_vm_stack_function_leave(stack, NULL, NULL, NULL));
  }
  IREE_RETURN_IF_ERROR(
      iree_allocator_free(stack->allocator, stack->register_storage));
  return IREE_STATUS_OK;
}

IREE_API_EXPORT iree_vm_stack_frame_t* IREE_API_CALL
iree_vm_stack_current_frame(iree_vm_stack_t* stack) {
  return stack->depth > 0 ? &stack->frames[stack->depth - 1] : NULL;
}

IREE_API_EXPORT iree_vm_stack_frame_t* IREE_API_CALL
iree_vm_stack_parent_frame(iree_vm_stack_t* stack) {
  return stack->depth > 1 ? &stack->frames[stack->depth - 2] : NULL;
}

IREE_API_EXPORT iree_status_t IREE_API_CALL iree_vm_stack_frame_registers(
    iree_vm_stack_t* stack, iree_vm_stack_frame_t* stack_frame,
    iree_vm_registers_t* out_registers) {
  if (!out_registers) return IREE_STATUS_INVALID_ARGUMENT;
  iree_vm_registers_from_base(stack, stack_frame->register_base, out_registers);
  return IREE_STATUS_OK;
}

// Remaps argument/result registers from a source list in the caller/callee
// frame to the 0-N ABI registers in the callee/caller frame. This assumes that
// the destination stack frame registers are unused and ok to overwrite
// directly.
static void iree_vm_stack_frame_remap_abi_registers(
    const iree_vm_registers_t src_regs,
    const iree_vm_register_list_t* src_reg_list,
    const iree_vm_registers_t dst_regs) {
  // Each bank begins left-aligned at 0 and increments per arg of its type.
  int i32_reg_offset = 0;
  int ref_reg_offset = 0;
  for (int i = 0; i < src_reg_list->size; ++i) {
    // TODO(benvanik): change encoding to avoid this branching.
    // Could write two arrays: one for prims and one for refs.
    uint16_t src_reg = src_reg_list->registers[i];
    if (src_reg & IREE_REF_REGISTER_TYPE_BIT) {
      uint16_t dst_reg = ref_reg_offset++;
      memset(&dst_regs.ref[dst_reg & dst_regs.ref_mask], 0,
             sizeof(iree_vm_ref_t));
      iree_vm_ref_retain_or_move(src_reg & IREE_REF_REGISTER_MOVE_BIT,
                                 &src_regs.ref[src_reg & src_regs.ref_mask],
                                 &dst_regs.ref[dst_reg & dst_regs.ref_mask]);
    } else {
      uint16_t dst_reg = i32_reg_offset++;
      dst_regs.i32[dst_reg & dst_regs.i32_mask] =
          src_regs.i32[src_reg & src_regs.i32_mask];
    }
  }
}

// Remaps registers from source to destination, possibly across frames.
// Registers from the |src_regs| will be copied/moved to |dst_regs| with the
// mappings provided by |src_reg_list| and |dst_reg_list|. It's assumed that the
// mappings are matching by type and - in the case that they aren't - things
// will get weird (but not crash).
static void iree_vm_stack_frame_remap_registers(
    const iree_vm_registers_t src_regs,
    const iree_vm_register_list_t* src_reg_list,
    const iree_vm_registers_t dst_regs,
    const iree_vm_register_list_t* dst_reg_list) {
  VMCHECK(src_reg_list->size == dst_reg_list->size);
  if (src_reg_list->size != dst_reg_list->size) return;
  for (int i = 0; i < src_reg_list->size; ++i) {
    // TODO(benvanik): change encoding to avoid this branching.
    // Could write two arrays: one for prims and one for refs.
    uint16_t src_reg = src_reg_list->registers[i];
    uint16_t dst_reg = dst_reg_list->registers[i];
    if (src_reg & IREE_REF_REGISTER_TYPE_BIT) {
      iree_vm_ref_retain_or_move(src_reg & IREE_REF_REGISTER_MOVE_BIT,
                                 &src_regs.ref[src_reg & src_regs.ref_mask],
                                 &dst_regs.ref[dst_reg & dst_regs.ref_mask]);
    } else {
      dst_regs.i32[dst_reg & dst_regs.i32_mask] =
          src_regs.i32[src_reg & src_regs.i32_mask];
    }
  }
}
IREE_API_EXPORT iree_status_t IREE_API_CALL iree_vm_stack_function_enter(
    iree_vm_stack_t* stack, iree_vm_function_t function,
    const iree_vm_register_list_t* argument_registers,
    iree_vm_stack_frame_t** out_callee_frame,
    iree_vm_registers_t* out_callee_registers) {
  if (out_callee_frame) {
    *out_callee_frame = NULL;
  }
  if (out_callee_registers) {
    memset(out_callee_registers, 0, sizeof(*out_callee_registers));
  }
  if (stack->depth == IREE_MAX_STACK_DEPTH) {
    return IREE_STATUS_RESOURCE_EXHAUSTED;
  }

  // Setup the callee frame.
  iree_vm_stack_frame_t* caller_frame =
      stack->depth ? &stack->frames[stack->depth - 1] : NULL;
  iree_vm_stack_frame_t* callee_frame = &stack->frames[stack->depth];
  ++stack->depth;
  callee_frame->function = function;
  callee_frame->pc = 0;
  callee_frame->return_registers = NULL;

  // Try to reuse the same module state if the caller and callee are from the
  // same module. Otherwise, query the state from the registered handler.
  if (caller_frame) {
    if (caller_frame->function.module == function.module) {
      callee_frame->module_state = caller_frame->module_state;
    }
  }
  if (!callee_frame->module_state) {
    IREE_RETURN_IF_ERROR(stack->state_resolver.query_module_state(
        stack->state_resolver.self, function.module,
        &callee_frame->module_state));
  }

  // Allocate register storage, initially empty.
  iree_vm_registers_t callee_registers;
  IREE_RETURN_IF_ERROR(iree_vm_stack_reserve_register_storage(
      stack, function, &callee_registers));
  iree_vm_registers_to_base(stack, callee_registers,
                            &callee_frame->register_base);

  // Remap arguments from the caller stack frame into the callee stack frame.
  if (caller_frame && argument_registers) {
    iree_vm_registers_t caller_registers;
    iree_vm_registers_from_base(stack, caller_frame->register_base,
                                &caller_registers);
    iree_vm_stack_frame_remap_abi_registers(
        caller_registers, argument_registers, callee_registers);
  }

  if (out_callee_frame) *out_callee_frame = callee_frame;
  if (out_callee_registers) *out_callee_registers = callee_registers;
  return IREE_STATUS_OK;
}

IREE_API_EXPORT iree_status_t IREE_API_CALL iree_vm_stack_function_leave(
    iree_vm_stack_t* stack, const iree_vm_register_list_t* result_registers,
    iree_vm_stack_frame_t** out_caller_frame,
    iree_vm_registers_t* out_caller_registers) {
  if (stack->depth <= 0) {
    return IREE_STATUS_FAILED_PRECONDITION;
  }

  iree_vm_stack_frame_t* callee_frame = &stack->frames[stack->depth - 1];
  iree_vm_stack_frame_t* caller_frame =
      stack->depth > 1 ? &stack->frames[stack->depth - 2] : NULL;

  // Remap result registers from the callee frame to the caller frame.
  iree_vm_registers_t caller_registers;
  memset(&caller_registers, 0, sizeof(caller_registers));
  if (caller_frame && result_registers) {
    iree_vm_registers_t callee_registers;
    iree_vm_registers_from_base(stack, callee_frame->register_base,
                                &callee_registers);
    iree_vm_registers_from_base(stack, caller_frame->register_base,
                                &caller_registers);
    iree_vm_stack_frame_remap_registers(callee_registers, result_registers,
                                        caller_registers,
                                        caller_frame->return_registers);
  }

  // Release the reserved register storage to restore the frame pointer.
  iree_vm_stack_release_register_storage(stack, callee_frame);

  // Pop stack and zero out (to make debugging easier).
  --stack->depth;
  memset(&callee_frame->function, 0, sizeof(callee_frame->function));
  callee_frame->module_state = NULL;

  if (out_caller_frame) *out_caller_frame = caller_frame;
  if (out_caller_registers) *out_caller_registers = caller_registers;
  return IREE_STATUS_OK;
}

// Counts the types of each register required for the given variant list.
static void iree_vm_stack_frame_count_register_types(
    iree_vm_variant_list_t* inputs, iree_host_size_t* out_i32_reg_count,
    iree_host_size_t* out_ref_reg_count) {
  iree_host_size_t count = iree_vm_variant_list_size(inputs);
  uint16_t i32_reg = 0;
  uint16_t ref_reg = 0;
  for (iree_host_size_t i = 0; i < count; ++i) {
    iree_vm_variant_t* variant = iree_vm_variant_list_get(inputs, i);
    if (IREE_VM_VARIANT_IS_REF(variant)) {
      ++ref_reg;
    } else {
      ++i32_reg;
    }
  }
  *out_i32_reg_count = i32_reg;
  *out_ref_reg_count = ref_reg;
}

// Marshals a variant list of values into callee registers.
// The |out_dst_reg_list| will be populated with the register ordinals and must
// be preallocated to store iree_vm_variant_list_size inputs.
static void iree_vm_stack_frame_marshal_inputs(
    iree_vm_variant_list_t* inputs, const iree_vm_registers_t dst_regs,
    iree_vm_register_list_t* out_dst_reg_list) {
  iree_host_size_t count = iree_vm_variant_list_size(inputs);
  uint16_t i32_reg = 0;
  uint16_t ref_reg = 0;
  out_dst_reg_list->size = (uint16_t)count;
  for (iree_host_size_t i = 0; i < count; ++i) {
    iree_vm_variant_t* variant = iree_vm_variant_list_get(inputs, i);
    if (IREE_VM_VARIANT_IS_REF(variant)) {
      out_dst_reg_list->registers[i] =
          ref_reg | IREE_REF_REGISTER_TYPE_BIT | IREE_REF_REGISTER_MOVE_BIT;
      iree_vm_ref_t* reg_ref = &dst_regs.ref[ref_reg++];
      memset(reg_ref, 0, sizeof(*reg_ref));
      iree_vm_ref_retain(&variant->ref, reg_ref);
    } else {
      out_dst_reg_list->registers[i] = i32_reg;
      dst_regs.i32[i32_reg++] = variant->i32;
    }
  }
}

// Marshals callee return registers into a variant list.
// |src_reg_list| defines
static iree_status_t iree_vm_stack_frame_marshal_outputs(
    const iree_vm_registers_t src_regs,
    const iree_vm_register_list_t* src_reg_list,
    iree_vm_variant_list_t* outputs) {
  for (int i = 0; i < src_reg_list->size; ++i) {
    uint16_t reg = src_reg_list->registers[i];
    if (reg & IREE_REF_REGISTER_TYPE_BIT) {
      iree_vm_ref_t* value = &src_regs.ref[reg & src_regs.ref_mask];
      IREE_RETURN_IF_ERROR(
          iree_vm_variant_list_append_ref_move(outputs, value));
    } else {
      iree_vm_value_t value;
      value.type = IREE_VM_VALUE_TYPE_I32;
      value.i32 = src_regs.i32[reg & src_regs.i32_mask];
      IREE_RETURN_IF_ERROR(iree_vm_variant_list_append_value(outputs, value));
    }
  }
  return IREE_STATUS_OK;
}

IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_vm_stack_function_enter_external(
    iree_vm_stack_t* stack, iree_vm_variant_list_t* arguments,
    iree_vm_register_list_t* out_argument_registers) {
  out_argument_registers->size = 0;
  if (stack->depth == IREE_MAX_STACK_DEPTH) {
    return IREE_STATUS_RESOURCE_EXHAUSTED;
  }

  // Compute required register counts to marshal in the arguments.
  iree_host_size_t i32_reg_count = 0;
  iree_host_size_t ref_reg_count = 0;
  if (arguments) {
    iree_vm_stack_frame_count_register_types(arguments, &i32_reg_count,
                                             &ref_reg_count);
  }

  // Create a top-level stack frame to pass args/results to the VM.
  // This will be displayed as [native method] or something in traces.
  iree_vm_function_t native_function;
  memset(&native_function, 0, sizeof(native_function));
  // NOTE: this is overallocating because we don't know this information yet.
  native_function.i32_register_count = max_register_count;
  native_function.ref_register_count = max_register_count;

  // Setup the native frame.
  iree_vm_stack_frame_t* callee_frame = &stack->frames[stack->depth];
  ++stack->depth;
  callee_frame->function = native_function;
  callee_frame->pc = 0;
  callee_frame->return_registers = NULL;

  // Allocate register storage, initially empty.
  iree_vm_registers_t callee_registers;
  IREE_RETURN_IF_ERROR(iree_vm_stack_reserve_register_storage(
      stack, native_function, &callee_registers));
  iree_vm_registers_to_base(stack, callee_registers,
                            &callee_frame->register_base);

  // Marshal inputs into the stack frame.
  if (arguments) {
    iree_vm_stack_frame_marshal_inputs(arguments, callee_registers,
                                       out_argument_registers);
  }

  return IREE_STATUS_OK;
}

IREE_API_EXPORT iree_status_t IREE_API_CALL
iree_vm_stack_function_leave_external(iree_vm_stack_t* stack,
                                      iree_vm_variant_list_t* results) {
  if (stack->depth <= 0) {
    return IREE_STATUS_FAILED_PRECONDITION;
  }

  iree_vm_stack_frame_t* callee_frame = &stack->frames[stack->depth - 1];

  // Marshal return registers to results.
  if (results) {
    iree_vm_registers_t callee_registers;
    iree_vm_registers_from_base(stack, callee_frame->register_base,
                                &callee_registers);
    IREE_RETURN_IF_ERROR(iree_vm_stack_frame_marshal_outputs(
        callee_registers, callee_frame->return_registers, results));
  }

  // Release the reserved register storage to restore the frame pointer.
  iree_vm_stack_release_register_storage(stack, callee_frame);

  // Pop stack and zero out (to make debugging easier).
  --stack->depth;
  memset(&callee_frame->function, 0, sizeof(callee_frame->function));
  callee_frame->module_state = NULL;

  return IREE_STATUS_OK;
}
