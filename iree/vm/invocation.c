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

#include "iree/vm/invocation.h"

#include "iree/base/api.h"

static iree_status_t iree_vm_validate_function_inputs(
    iree_vm_function_t function, iree_vm_variant_list_t* inputs) {
  // TODO(benvanik): validate inputs.
  return IREE_STATUS_OK;
}

// TODO(benvanik): implement this as an iree_vm_invocation_t sequence.
IREE_API_EXPORT iree_status_t IREE_API_CALL iree_vm_invoke(
    iree_vm_context_t* context, iree_vm_function_t function,
    const iree_vm_invocation_policy_t* policy, iree_vm_variant_list_t* inputs,
    iree_vm_variant_list_t* outputs, iree_allocator_t allocator) {
  // NOTE: it is ok to have no inputs or outputs. If we do have them, though,
  // they must be valid.
  // TODO(benvanik): validate outputs capacity.
  IREE_RETURN_IF_ERROR(iree_vm_validate_function_inputs(function, inputs));

  // Allocate a VM stack and initialize it.
  iree_vm_stack_t stack;
  IREE_RETURN_IF_ERROR(iree_vm_stack_init(
      iree_vm_context_state_resolver(context), allocator, &stack));

  // Setup the [external] marshaling stack frame to pass the inputs and get
  // the outputs.
  iree_vm_register_list_t* argument_registers =
      (iree_vm_register_list_t*)iree_alloca(sizeof(iree_vm_register_list_t) +
                                            iree_vm_variant_list_size(inputs) *
                                                sizeof(uint16_t));
  IREE_RETURN_IF_ERROR(iree_vm_stack_function_enter_external(
      &stack, inputs, argument_registers));

  // Perform execution. Note that for synchronous execution we expect this to
  // complete without yielding.
  iree_vm_execution_result_t result;
  iree_status_t call_status = function.module->call(
      function.module->self, &stack, function, argument_registers, &result);
  iree_freea(argument_registers);
  IREE_RETURN_IF_ERROR(call_status);

  // Read back the outputs from the [external] marshaling stack frame.
  IREE_RETURN_IF_ERROR(iree_vm_stack_function_leave_external(&stack, outputs));

  IREE_RETURN_IF_ERROR(iree_vm_stack_deinit(&stack));

  return IREE_STATUS_OK;
}
