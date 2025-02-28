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

#ifndef IREE_TOOLS_UTILS_IMAGE_UTIL_H_
#define IREE_TOOLS_UTILS_IMAGE_UTIL_H_

#include "iree/base/api.h"
#include "iree/hal/api.h"
#include "iree/hal/buffer_view.h"

#if __cplusplus
extern "C" {
#endif  // __cplusplus

// Loads the image at |filename| into |out_pixel_data| and sets
// |out_buffer_length| to its length.
//
// The image dimension must match the width, height, and channel in|shape|,
// while 2 <= |shape_rank| <= 4 to match the image tensor format.
//
// The file must be in a format supported by stb_image.h.
// The returned |out_pixel_data| buffer must be released by the caller.
iree_status_t iree_tools_utils_load_pixel_data(
    const iree_string_view_t filename, const iree_hal_dim_t* shape,
    iree_host_size_t shape_rank, iree_hal_element_type_t element_type,
    uint8_t** out_pixel_data, iree_host_size_t* out_buffer_length);

// Parse the content in an image file in |filename| into a HAL buffer view
// |out_buffer_view|. |out_buffer_view| properties are defined by |shape|,
// |shape_rank|, and |element_type|, while being allocated by |allocator|.
//
// The |element_type| has to be SINT_8 or UINT_8. For FLOAT_32, use
// |iree_tools_utils_buffer_view_from_image_rescaled| instead.
//
// The returned |out_buffer_view| must be released by the caller.
iree_status_t iree_tools_utils_buffer_view_from_image(
    const iree_string_view_t filename, const iree_hal_dim_t* shape,
    iree_host_size_t shape_rank, iree_hal_element_type_t element_type,
    iree_hal_allocator_t* allocator, iree_hal_buffer_view_t** out_buffer_view);

// Parse the content in an image file in |filename| into a HAL buffer view
// |out_buffer_view|. |out_buffer_view| properties are defined by |shape|,
// |shape_rank|, and |element_type|, while being allocated by |allocator|.
// The value in |out_buffer_view| is rescaled with |input_range|.
//
// The |element_type| has to be FLOAT_32, For SINT_8 or UINT_8, use
// |iree_tools_utils_buffer_view_from_image| instead.
//
// The returned |out_buffer_view| must be released by the caller.
iree_status_t iree_tools_utils_buffer_view_from_image_rescaled(
    const iree_string_view_t filename, const iree_hal_dim_t* shape,
    iree_host_size_t shape_rank, iree_hal_element_type_t element_type,
    iree_hal_allocator_t* allocator, const float* input_range,
    iree_host_size_t range_length, iree_hal_buffer_view_t** out_buffer_view);

// Normalize uint8_t |pixel data| of the size |buffer_length| to float buffer
// |out_buffer| with the range |input_range|.
//
// float32_x = (uint8_x - 127.5) / 127.5 * input_scale + input_offset, where
// input_scale = abs(|input_range[0]| - |input_range[1]| / 2
// input_offset = |input_range[0]| + |input_range[1]| / 2
//
// |out_buffer| needs to be allocated before the call.
iree_status_t iree_tools_utils_pixel_rescaled_to_buffer(
    const uint8_t* pixel_data, iree_host_size_t buffer_length,
    const float* input_range, iree_host_size_t range_length, float* out_buffer);

#if __cplusplus
}
#endif  // __cplusplus

#endif  // IREE_TOOLS_UTILS_IMAGE_UTIL_H_
