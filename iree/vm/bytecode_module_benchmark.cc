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

#include "absl/container/inlined_vector.h"
#include "absl/strings/string_view.h"
#include "benchmark/benchmark.h"
#include "iree/base/api.h"
#include "iree/base/logging.h"
#include "iree/vm/bytecode_module.h"
#include "iree/vm/bytecode_module_benchmark_module.h"
#include "iree/vm/module.h"
#include "iree/vm/stack.h"

namespace {

// Example import function that adds 1 to its value.
static iree_status_t IREE_API_CALL SimpleAddExecute(
    void* self, iree_vm_stack_t* stack, iree_vm_function_t function,
    const iree_vm_register_list_t* argument_registers,
    iree_vm_execution_result_t* out_result) {
  iree_vm_stack_frame_t* frame;
  iree_vm_registers_t registers;
  iree_vm_stack_function_enter(stack, function, argument_registers, &frame,
                               &registers);

  int32_t value = registers.i32[0];
  registers.i32[0] = value + 1;

  // TODO(benvanik): replace with macro? helper for none/i32/etc
  static const union {
    uint16_t reserved[2];
    iree_vm_register_list_t list;
  } result_registers = {{1, 0}};
  iree_vm_stack_function_leave(stack, &result_registers.list, NULL, NULL);
  return IREE_STATUS_OK;
}

// Benchmarks the given exported function, optionally passing in arguments.
static iree_status_t RunFunction(benchmark::State& state,
                                 absl::string_view function_name,
                                 absl::InlinedVector<int32_t, 4> i32_args,
                                 int batch_size = 1) {
  const auto* module_file_toc =
      iree::vm::bytecode_module_benchmark_module_create();
  iree_vm_module_t* module = nullptr;
  IREE_CHECK_OK(iree_vm_bytecode_module_create(
      iree_const_byte_span_t{
          reinterpret_cast<const uint8_t*>(module_file_toc->data),
          module_file_toc->size},
      IREE_ALLOCATOR_NULL, IREE_ALLOCATOR_SYSTEM, &module))
      << "Bytecode module failed to load";

  iree_vm_module_state_t* module_state;
  module->alloc_state(module->self, IREE_ALLOCATOR_SYSTEM, &module_state);

  iree_vm_module_t import_module;
  memset(&import_module, 0, sizeof(import_module));
  import_module.call = SimpleAddExecute;
  iree_vm_function_t imported_func;
  imported_func.module = &import_module;
  imported_func.linkage = IREE_VM_FUNCTION_LINKAGE_INTERNAL;
  imported_func.ordinal = 0;
  module->resolve_import(module->self, module_state, 0, imported_func);

  // Since we only have a single state we pack it in the state_resolver ptr.
  iree_vm_state_resolver_t state_resolver = {
      module_state,
      +[](void* state_resolver, iree_vm_module_t* module,
          iree_vm_module_state_t** out_module_state) -> iree_status_t {
        *out_module_state = (iree_vm_module_state_t*)state_resolver;
        return IREE_STATUS_OK;
      }};

  iree_vm_stack_t stack;
  iree_vm_stack_init(state_resolver, IREE_ALLOCATOR_SYSTEM, &stack);

  iree_vm_function_t function;
  IREE_CHECK_OK(module->lookup_function(
      module, IREE_VM_FUNCTION_LINKAGE_EXPORT,
      iree_string_view_t{function_name.data(), function_name.size()},
      &function))
      << "Exported function '" << function_name << "' not found";

  iree_vm_variant_list_t* arguments = NULL;
  iree_vm_variant_list_alloc(i32_args.size(), IREE_ALLOCATOR_SYSTEM,
                             &arguments);
  for (int i = 0; i < i32_args.size(); ++i) {
    iree_vm_variant_list_append_value(arguments,
                                      IREE_VM_VALUE_MAKE_I32(i32_args[i]));
  }
  union {
    uint16_t reserved[2];
    iree_vm_register_list_t list;
  } argument_registers = {{0}};

  while (state.KeepRunningBatch(batch_size)) {
    iree_vm_stack_function_enter_external(&stack, arguments,
                                          &argument_registers.list);
    iree_vm_execution_result_t result;
    IREE_CHECK_OK(module->call(module->self, &stack, function,
                               &argument_registers.list, &result));
    iree_vm_stack_function_leave_external(&stack, NULL);
  }

  iree_vm_variant_list_free(arguments);

  iree_vm_stack_deinit(&stack);

  module->free_state(module->self, module_state);
  module->destroy(module->self);

  return IREE_STATUS_OK;
}

static void BM_ModuleCreate(benchmark::State& state) {
  while (state.KeepRunning()) {
    const auto* module_file_toc =
        iree::vm::bytecode_module_benchmark_module_create();
    iree_vm_module_t* module = nullptr;
    IREE_CHECK_OK(iree_vm_bytecode_module_create(
        iree_const_byte_span_t{
            reinterpret_cast<const uint8_t*>(module_file_toc->data),
            module_file_toc->size},
        IREE_ALLOCATOR_NULL, IREE_ALLOCATOR_SYSTEM, &module))
        << "Bytecode module failed to load";

    // Just testing creation and verification here!
    benchmark::DoNotOptimize(module);

    module->destroy(module->self);
  }
}
BENCHMARK(BM_ModuleCreate);

static void BM_ModuleCreateState(benchmark::State& state) {
  const auto* module_file_toc =
      iree::vm::bytecode_module_benchmark_module_create();
  iree_vm_module_t* module = nullptr;
  IREE_CHECK_OK(iree_vm_bytecode_module_create(
      iree_const_byte_span_t{
          reinterpret_cast<const uint8_t*>(module_file_toc->data),
          module_file_toc->size},
      IREE_ALLOCATOR_NULL, IREE_ALLOCATOR_SYSTEM, &module))
      << "Bytecode module failed to load";

  while (state.KeepRunning()) {
    iree_vm_module_state_t* module_state;
    module->alloc_state(module->self, IREE_ALLOCATOR_SYSTEM, &module_state);

    // Really just testing malloc overhead, though it'll be module-dependent
    // and if we do anything heavyweight on state init it'll show here.
    benchmark::DoNotOptimize(module_state);

    module->free_state(module->self, module_state);
  }

  module->destroy(module->self);
}
BENCHMARK(BM_ModuleCreateState);

static void BM_FullModuleInit(benchmark::State& state) {
  while (state.KeepRunning()) {
    const auto* module_file_toc =
        iree::vm::bytecode_module_benchmark_module_create();
    iree_vm_module_t* module = nullptr;
    IREE_CHECK_OK(iree_vm_bytecode_module_create(
        iree_const_byte_span_t{
            reinterpret_cast<const uint8_t*>(module_file_toc->data),
            module_file_toc->size},
        IREE_ALLOCATOR_NULL, IREE_ALLOCATOR_SYSTEM, &module))
        << "Bytecode module failed to load";

    iree_vm_module_state_t* module_state;
    module->alloc_state(module->self, IREE_ALLOCATOR_SYSTEM, &module_state);

    benchmark::DoNotOptimize(module_state);

    module->free_state(module->self, module_state);
    module->destroy(module->self);
  }
}
BENCHMARK(BM_FullModuleInit);

static void BM_EmptyFuncReference(benchmark::State& state) {
  static auto empty_fn = +[]() {
    int ret = 1;
    benchmark::DoNotOptimize(ret);
    return ret;
  };
  while (state.KeepRunning()) {
    int ret = empty_fn();
    benchmark::DoNotOptimize(ret);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_EmptyFuncReference);

static void BM_EmptyFuncBytecode(benchmark::State& state) {
  IREE_CHECK_OK(RunFunction(state, "empty_func", {}));
}
BENCHMARK(BM_EmptyFuncBytecode);

static void BM_CallInternalFuncReference(benchmark::State& state) {
  static auto add_fn = +[](int value) {
    benchmark::DoNotOptimize(value += value);
    return value;
  };
  while (state.KeepRunningBatch(10)) {
    int value = 1;
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    value = add_fn(value);
    benchmark::DoNotOptimize(value);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_CallInternalFuncReference);

static void BM_CallInternalFuncBytecode(benchmark::State& state) {
  IREE_CHECK_OK(RunFunction(state, "call_internal_func", {100},
                            /*batch_size=*/10));
}
BENCHMARK(BM_CallInternalFuncBytecode);

static void BM_CallImportedFuncBytecode(benchmark::State& state) {
  IREE_CHECK_OK(RunFunction(state, "call_imported_func", {100},
                            /*batch_size=*/10));
}
BENCHMARK(BM_CallImportedFuncBytecode);

static void BM_LoopSumReference(benchmark::State& state) {
  static auto loop = +[](int count) {
    int i = 0;
    for (; i < count; ++i) {
      benchmark::DoNotOptimize(i);
    }
    return i;
  };
  while (state.KeepRunningBatch(state.range(0))) {
    int ret = loop(state.range(0));
    benchmark::DoNotOptimize(ret);
    benchmark::ClobberMemory();
  }
}
BENCHMARK(BM_LoopSumReference)->Arg(100000);

static void BM_LoopSumBytecode(benchmark::State& state) {
  IREE_CHECK_OK(RunFunction(state, "loop_sum",
                            {static_cast<int32_t>(state.range(0))},
                            /*batch_size=*/state.range(0)));
}
BENCHMARK(BM_LoopSumBytecode)->Arg(100000);

}  // namespace
