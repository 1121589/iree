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

#include <array>

#include "benchmark/benchmark.h"
#include "iree/base/internal/file_io.h"
#include "iree/base/internal/flags.h"
#include "iree/base/status.h"
#include "iree/base/tracing.h"
#include "iree/hal/drivers/init.h"
#include "iree/modules/hal/hal_module.h"
#include "iree/tools/utils/vm_util.h"
#include "iree/vm/api.h"
#include "iree/vm/bytecode_module.h"

IREE_FLAG(string, module_file, "-",
          "File containing the module to load that contains the entry "
          "function. Defaults to stdin.");

// TODO(hanchung): Extract the batch size using
// iree_vm_function_reflection_attr.
IREE_FLAG(
    int32_t, batch_size, 1,
    "The number of batch size, which is expected to match "
    "iree-hal-benchmark-dispatch-repeat-count when translating the module");

IREE_FLAG(string, entry_function, "",
          "Name of a function contained in the module specified by module_file "
          "to run. If this is not set, all the exported functions will be "
          "benchmarked and they are expected to not have input arguments.");

IREE_FLAG(string, driver, "vmla", "Backend driver to use.");

static iree_status_t parse_function_input(iree_string_view_t flag_name,
                                          void* storage,
                                          iree_string_view_t value) {
  auto* list = (std::vector<std::string>*)storage;
  list->push_back(std::string(value.data, value.size));
  return iree_ok_status();
}
static void print_function_input(iree_string_view_t flag_name, void* storage,
                                 FILE* file) {
  auto* list = (std::vector<std::string>*)storage;
  if (list->empty()) {
    fprintf(file, "# --%.*s=\n", (int)flag_name.size, flag_name.data);
  } else {
    for (size_t i = 0; i < list->size(); ++i) {
      fprintf(file, "--%.*s=\"%s\"\n", (int)flag_name.size, flag_name.data,
              list->at(i).c_str());
    }
  }
}
static std::vector<std::string> FLAG_function_inputs;
IREE_FLAG_CALLBACK(
    parse_function_input, print_function_input, &FLAG_function_inputs,
    function_input,
    "An input value or buffer of the format:\n"
    "  [shape]xtype=[value]\n"
    "  2x2xi32=1 2 3 4\n"
    "Optionally, brackets may be used to separate the element values:\n"
    "  2x2xi32=[[1 2][3 4]]\n"
    "Each occurrence of the flag indicates an input in the order they were\n"
    "specified on the command line.");

namespace iree {
namespace {

static void BenchmarkFunction(const std::string& benchmark_name, int batch_size,
                              iree_vm_context_t* context,
                              iree_vm_function_t function,
                              iree_vm_list_t* inputs, benchmark::State& state) {
  IREE_TRACE_SCOPE_DYNAMIC(benchmark_name.c_str());
  IREE_TRACE_FRAME_MARK();

  // Benchmarking loop.
  while (state.KeepRunningBatch(batch_size)) {
    IREE_TRACE_SCOPE0("BenchmarkIteration");
    IREE_TRACE_FRAME_MARK_NAMED("Iteration");
    vm::ref<iree_vm_list_t> outputs;
    IREE_CHECK_OK(iree_vm_list_create(/*element_type=*/nullptr, 16,
                                      iree_allocator_system(), &outputs));
    IREE_CHECK_OK(iree_vm_invoke(context, function, /*policy=*/nullptr, inputs,
                                 outputs.get(), iree_allocator_system()));
  }
}

void RegisterModuleBenchmarks(const std::string& function_name,
                              iree_vm_context_t* context,
                              iree_vm_function_t function,
                              iree_vm_list_t* inputs) {
  auto benchmark_name = "BM_" + function_name;
  int batch_size = FLAG_batch_size;
  benchmark::RegisterBenchmark(benchmark_name.c_str(),
                               [benchmark_name, batch_size, context, function,
                                inputs](benchmark::State& state) -> void {
                                 BenchmarkFunction(benchmark_name, batch_size,
                                                   context, function, inputs,
                                                   state);
                               })
      // By default only the main thread is included in CPU time. Include all
      // the threads instead.
      ->MeasureProcessCPUTime()
      // To make single and multi-threaded benchmarks more comparable, use the
      // wall time to determine how many iterations to run. See
      // https://github.com/google/benchmark#cpu-timers,
      ->UseRealTime()
      // Report timing in milliseconds, which is the general order of magnitude
      // of model runs. The benchmark framework will print with precision
      // between 0 and 3 places after the decimal while aiming for three
      // significant digits. If we end up wanting precision beyond microseconds,
      // we can make this setting configurable with a custom command line flag.
      ->Unit(benchmark::kMillisecond);
}

iree_status_t GetModuleContentsFromFlags(std::string* out_contents) {
  IREE_TRACE_SCOPE0("GetModuleContentsFromFlags");
  auto module_file = std::string(FLAG_module_file);
  if (module_file == "-") {
    *out_contents = std::string{std::istreambuf_iterator<char>(std::cin),
                                std::istreambuf_iterator<char>()};
  } else {
    IREE_RETURN_IF_ERROR(GetFileContents(module_file.c_str(), out_contents));
  }
  return iree_ok_status();
}

// TODO(hanchung): Consider to refactor this out and reuse in iree-run-module.
// This class helps organize required resources for IREE. The order of
// construction and destruction for resources matters. And the lifetime of
// resources also matters. The lifetime of IREEBenchmark should be as long as
// ::benchmark::RunSpecifiedBenchmarks() where the resources are used during
// benchmarking.
class IREEBenchmark {
 public:
  IREEBenchmark() = default;

  ~IREEBenchmark() {
    IREE_TRACE_SCOPE0("IREEBenchmark::dtor");

    // Order matters.
    inputs_.reset();
    iree_vm_context_release(context_);
    iree_vm_module_release(hal_module_);
    iree_vm_module_release(input_module_);
    iree_hal_device_release(device_);
    iree_vm_instance_release(instance_);
  };

  iree_status_t Register() {
    IREE_TRACE_SCOPE0("IREEBenchmark::Register");

    if (!instance_ || !device_ || !hal_module_ || !context_ || !input_module_) {
      IREE_RETURN_IF_ERROR(Init());
    }

    auto function_name = std::string(FLAG_entry_function);
    if (!function_name.empty()) {
      IREE_RETURN_IF_ERROR(RegisterSpecificFunction(function_name));
    } else {
      IREE_RETURN_IF_ERROR(RegisterAllExportedFunctions());
    }
    return iree_ok_status();
  }

 private:
  iree_status_t Init() {
    IREE_TRACE_SCOPE0("IREEBenchmark::Init");
    IREE_TRACE_FRAME_MARK_BEGIN_NAMED("init");

    IREE_RETURN_IF_ERROR(GetModuleContentsFromFlags(&module_data_));

    IREE_RETURN_IF_ERROR(iree_hal_module_register_types());
    IREE_RETURN_IF_ERROR(
        iree_vm_instance_create(iree_allocator_system(), &instance_));

    // Create IREE's device and module.
    IREE_RETURN_IF_ERROR(iree::CreateDevice(FLAG_driver, &device_));
    IREE_RETURN_IF_ERROR(CreateHalModule(device_, &hal_module_));
    IREE_RETURN_IF_ERROR(LoadBytecodeModule(module_data_, &input_module_));

    // Order matters. The input module will likely be dependent on the hal
    // module.
    std::array<iree_vm_module_t*, 2> modules = {hal_module_, input_module_};
    IREE_RETURN_IF_ERROR(iree_vm_context_create_with_modules(
        instance_, modules.data(), modules.size(), iree_allocator_system(),
        &context_));

    IREE_TRACE_FRAME_MARK_END_NAMED("init");
    return iree_ok_status();
  }

  iree_status_t RegisterSpecificFunction(const std::string& function_name) {
    IREE_TRACE_SCOPE0("IREEBenchmark::RegisterSpecificFunction");

    iree_vm_function_t function;
    IREE_RETURN_IF_ERROR(input_module_->lookup_function(
        input_module_->self, IREE_VM_FUNCTION_LINKAGE_EXPORT,
        iree_string_view_t{function_name.data(), function_name.size()},
        &function));

    IREE_CHECK_OK(ParseToVariantList(iree_hal_device_allocator(device_),
                                     FLAG_function_inputs, &inputs_));
    RegisterModuleBenchmarks(function_name, context_, function, inputs_.get());
    return iree_ok_status();
  }

  iree_status_t RegisterAllExportedFunctions() {
    IREE_TRACE_SCOPE0("IREEBenchmark::RegisterAllExportedFunctions");
    iree_vm_function_t function;
    iree_vm_module_signature_t signature =
        input_module_->signature(input_module_->self);
    for (iree_host_size_t i = 0; i < signature.export_function_count; ++i) {
      iree_string_view_t export_name;
      IREE_CHECK_OK(input_module_->get_function(
          input_module_->self, IREE_VM_FUNCTION_LINKAGE_EXPORT, i, &function,
          &export_name, nullptr));

      // We run anything with the 'benchmark' attribute.
      // If the attribute is not present we'll run anything that looks runnable.
      bool known_benchmark = !iree_string_view_is_empty(
          iree_vm_function_reflection_attr(&function, IREE_SV("benchmark")));
      if (!known_benchmark) {
        if (iree_string_view_starts_with(export_name,
                                         iree_make_cstring_view("__")) ||
            iree_string_view_find_char(export_name, '$', 0) !=
                IREE_STRING_VIEW_NPOS) {
          // Skip internal or special functions.
          continue;
        }

        iree_vm_function_signature_t signature =
            iree_vm_function_signature(&function);
        iree_host_size_t argument_count = 0;
        iree_host_size_t result_count = 0;
        IREE_RETURN_IF_ERROR(iree_vm_function_call_count_arguments_and_results(
            &signature, &argument_count, &result_count));
        if (argument_count) {
          // Only functions with no inputs are run (because we can't pass
          // anything).
          continue;
        }
      }

      iree::RegisterModuleBenchmarks(
          std::string(export_name.data, export_name.size), context_, function,
          /*inputs=*/nullptr);
    }
    return iree_ok_status();
  }

  std::string module_data_;
  iree_vm_instance_t* instance_ = nullptr;
  iree_hal_device_t* device_ = nullptr;
  iree_vm_module_t* hal_module_ = nullptr;
  iree_vm_context_t* context_ = nullptr;
  iree_vm_module_t* input_module_ = nullptr;
  iree::vm::ref<iree_vm_list_t> inputs_;
};
}  // namespace
}  // namespace iree

int main(int argc, char** argv) {
  IREE_TRACE_SCOPE0("main");

  // Pass through flags to benchmark (allowing --help to fall through).
  iree_flags_parse_checked(IREE_FLAGS_PARSE_MODE_UNDEFINED_OK |
                               IREE_FLAGS_PARSE_MODE_CONTINUE_AFTER_HELP,
                           &argc, &argv);
  ::benchmark::Initialize(&argc, argv);

  IREE_CHECK_OK(iree_hal_register_all_available_drivers(
      iree_hal_driver_registry_default()));

  iree::IREEBenchmark iree_benchmark;
  iree_status_t status = iree_benchmark.Register();
  if (!iree_status_is_ok(status)) {
    int ret = static_cast<int>(iree_status_code(status));
    std::cout << iree::Status(std::move(status)) << std::endl;
    return ret;
  }
  ::benchmark::RunSpecifiedBenchmarks();
  return 0;
}
