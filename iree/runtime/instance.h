// Copyright 2021 Google LLC
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

#ifndef IREE_RUNTIME_INSTANCE_H_
#define IREE_RUNTIME_INSTANCE_H_

#include "iree/base/api.h"
#include "iree/hal/api.h"

#ifdef __cplusplus
extern "C" {
#endif  // __cplusplus

// Shared runtime instance responsible for isolating runtime usage, enumerating
// and creating hardware device interfaces, and managing device resource pools.
//
// A single runtime instance can service multiple sessions and hosting
// applications should try to reuse instances as much as possible. This ensures
// that resource allocation across contexts is handled and extraneous device
// interaction is avoided. For devices that may have exclusive access
// restrictions it is mandatory to share instances, so plan accordingly.
//
// In multi-tenant systems separate instances can be used to isolate each tenant
// in cases where the underlying devices do not cleanly support isolation
// themselves and otherwise multiple tenants can share the same instance.
// Consider an instance as isolating IREE from itself rather than being the only
// mechanism that can be used to isolate individual tenants or sessions.
//
// Caches and allocator pools are associated with an instance and resources may
// be reused among any sessions sharing the same instance. In multi-tenant
// environments where all tenants are trusted (and here "tenant" may just mean
// "a single session" where there are many sessions) then they can often receive
// large benefits in terms of peak memory consumption, startup time, and
// interoperation by sharing an instance. If two tenants must never share any
// data (PII) then they should be placed in different instances.
//
// As with all of iree/runtime/ this API is a higher-level wrapper for the
// low-level IREE HAL and VM. Using this may pull in additional dependencies and
// perform additional allocations compared to what you can get by directly going
// to the lower levels.
//
// Thread-safe.
typedef struct iree_runtime_instance_s iree_runtime_instance_t;

//===----------------------------------------------------------------------===//
// iree_runtime_instance_options_t
//===----------------------------------------------------------------------===//

// Options used to configure instance creation.
typedef struct {
  // Should be set to IREE_API_VERSION_LATEST.
  iree_api_version_t api_version;

  // TODO(benvanik): inject logging hooks.

  // A driver registry used to enumerate and create HAL devices.
  // When not provided a device must be specified when creating sessions via
  // iree_runtime_session_create_with_device.
  iree_hal_driver_registry_t* driver_registry;
} iree_runtime_instance_options_t;

// Initializes |out_options| to its default values.
IREE_API_EXPORT void iree_runtime_instance_options_initialize(
    iree_api_version_t api_version,
    iree_runtime_instance_options_t* out_options);

// Sets the instance to use all available drivers registered in the current
// binary. This allows for control over driver selection from the build system:
// if you have IREE_HAL_DRIVERS_TO_BUILD=Vulkan then you'll only get Vulkan.
// Sessions may query for the driver listing and select one(s) that are
// appropriate.
IREE_API_EXPORT void iree_runtime_instance_options_use_all_available_drivers(
    iree_runtime_instance_options_t* options);

//===----------------------------------------------------------------------===//
// iree_runtime_instance_t
//===----------------------------------------------------------------------===//

// Creates a new instance with the given |options|.
// Instances should be shared with as many sessions in an application as is
// reasonable to ensure that resources are tracked properly and threads are
// managed correctly.
//
// |host_allocator| will be used to allocate the instance and any associated
// resources. |out_instance| must be released by the caller.
IREE_API_EXPORT iree_status_t iree_runtime_instance_create(
    const iree_runtime_instance_options_t* options,
    iree_allocator_t host_allocator, iree_runtime_instance_t** out_instance);

// Retains the given |instance| for the caller.
IREE_API_EXPORT void iree_runtime_instance_retain(
    iree_runtime_instance_t* instance);

// Releases the given |instance| from the caller.
IREE_API_EXPORT void iree_runtime_instance_release(
    iree_runtime_instance_t* instance);

// Returns the host allocator used to allocate the instance and its resources.
// Callers should use this to allocate resources so that any memory tracking
// being performed correctly attributes the allocations to the instance.
IREE_API_EXPORT iree_allocator_t
iree_runtime_instance_host_allocator(const iree_runtime_instance_t* instance);

// Returns the optional driver registry used to enumerate drivers and devices.
// If not provided then iree_runtime_session_create_with_device must be used
// to specify the device that a session should use.
IREE_API_EXPORT iree_hal_driver_registry_t*
iree_runtime_instance_driver_registry(const iree_runtime_instance_t* instance);

// TODO(#5724): remove this once user modules query devices themselves.
IREE_API_EXPORT iree_status_t iree_runtime_instance_try_create_default_device(
    iree_runtime_instance_t* instance, iree_string_view_t driver_name,
    iree_hal_device_t** out_device);

#ifdef __cplusplus
}  // extern "C"
#endif  // __cplusplus

#endif  // IREE_RUNTIME_INSTANCE_H_
