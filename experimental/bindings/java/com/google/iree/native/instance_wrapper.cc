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

#include "experimental/bindings/java/com/google/iree/native/instance_wrapper.h"

#include <mutex>

#include "iree/base/internal/flags.h"
#include "iree/hal/vmla/registration/driver_module.h"
#include "iree/modules/hal/hal_module.h"
#include "iree/modules/strings/strings_module.h"
#include "iree/modules/tensorlist/native_module.h"

namespace iree {
namespace java {

namespace {

void SetupVm() {
  // TODO(jennik): Pass flags through from java and us iree_flags_parse.
  // This checked version will abort()/exit() and that's... not great.
  char binname[] = "libiree.so";
  char* argv[] = {binname};
  char** aargv = argv;
  int argc = 1;
  iree_flags_parse_checked(IREE_FLAGS_PARSE_MODE_DEFAULT, &argc, &aargv);

  // TODO(jennik): register all available drivers
  IREE_CHECK_OK(iree_hal_vmla_driver_module_register(
      iree_hal_driver_registry_default()));
  IREE_CHECK_OK(iree_vm_register_builtin_types());
  IREE_CHECK_OK(iree_hal_module_register_types());
  IREE_CHECK_OK(iree_tensorlist_module_register_types());
  IREE_CHECK_OK(iree_strings_module_register_types());
}

}  // namespace

Status InstanceWrapper::Create() {
  static std::once_flag setup_vm_once;
  std::call_once(setup_vm_once, [] { SetupVm(); });

  return iree_vm_instance_create(iree_allocator_system(), &instance_);
}

iree_vm_instance_t* InstanceWrapper::instance() const { return instance_; }

InstanceWrapper::~InstanceWrapper() { iree_vm_instance_release(instance_); }

}  // namespace java
}  // namespace iree
