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

#include "iree/hal/vulkan/native_event.h"

#include "iree/base/tracing.h"
#include "iree/hal/vulkan/status_util.h"

using namespace iree::hal::vulkan;

typedef struct {
  iree_hal_resource_t resource;
  VkDeviceHandle* logical_device;
  VkEvent handle;
} iree_hal_vulkan_native_event_t;

extern const iree_hal_event_vtable_t iree_hal_vulkan_native_event_vtable;

static iree_hal_vulkan_native_event_t* iree_hal_vulkan_native_event_cast(
    iree_hal_event_t* base_value) {
  IREE_HAL_ASSERT_TYPE(base_value, &iree_hal_vulkan_native_event_vtable);
  return (iree_hal_vulkan_native_event_t*)base_value;
}

static iree_status_t iree_hal_vulkan_create_event(
    VkDeviceHandle* logical_device, VkEvent* out_handle) {
  VkEventCreateInfo create_info;
  create_info.sType = VK_STRUCTURE_TYPE_EVENT_CREATE_INFO;
  create_info.pNext = NULL;
  create_info.flags = 0;
  return VK_RESULT_TO_STATUS(logical_device->syms()->vkCreateEvent(
                                 *logical_device, &create_info,
                                 logical_device->allocator(), out_handle),
                             "vkCreateEvent");
}

static void iree_hal_vulkan_destroy_event(VkDeviceHandle* logical_device,
                                          VkEvent handle) {
  if (handle == VK_NULL_HANDLE) return;
  logical_device->syms()->vkDestroyEvent(*logical_device, handle,
                                         logical_device->allocator());
}

iree_status_t iree_hal_vulkan_native_event_create(
    VkDeviceHandle* logical_device, iree_hal_event_t** out_event) {
  IREE_ASSERT_ARGUMENT(logical_device);
  IREE_ASSERT_ARGUMENT(out_event);
  *out_event = NULL;
  IREE_TRACE_ZONE_BEGIN(z0);

  VkEvent handle = VK_NULL_HANDLE;
  IREE_RETURN_AND_END_ZONE_IF_ERROR(
      z0, iree_hal_vulkan_create_event(logical_device, &handle));

  iree_hal_vulkan_native_event_t* event = NULL;
  iree_status_t status = iree_allocator_malloc(logical_device->host_allocator(),
                                               sizeof(*event), (void**)&event);
  if (iree_status_is_ok(status)) {
    iree_hal_resource_initialize(&iree_hal_vulkan_native_event_vtable,
                                 &event->resource);
    event->logical_device = logical_device;
    event->handle = handle;
    *out_event = (iree_hal_event_t*)event;
  } else {
    iree_hal_vulkan_destroy_event(logical_device, handle);
  }

  IREE_TRACE_ZONE_END(z0);
  return status;
}

static void iree_hal_vulkan_native_event_destroy(iree_hal_event_t* base_event) {
  iree_hal_vulkan_native_event_t* event =
      iree_hal_vulkan_native_event_cast(base_event);
  iree_allocator_t host_allocator = event->logical_device->host_allocator();
  IREE_TRACE_ZONE_BEGIN(z0);

  iree_hal_vulkan_destroy_event(event->logical_device, event->handle);
  iree_allocator_free(host_allocator, event);

  IREE_TRACE_ZONE_END(z0);
}

VkEvent iree_hal_vulkan_native_event_handle(
    const iree_hal_event_t* base_event) {
  return ((const iree_hal_vulkan_native_event_t*)base_event)->handle;
}

const iree_hal_event_vtable_t iree_hal_vulkan_native_event_vtable = {
    /*.destroy=*/iree_hal_vulkan_native_event_destroy,
};
