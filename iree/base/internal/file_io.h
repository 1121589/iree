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

#ifndef IREE_BASE_INTERNAL_FILE_IO_H_
#define IREE_BASE_INTERNAL_FILE_IO_H_

#include "iree/base/api.h"

#ifdef __cplusplus
extern "C" {
#endif

// Checks if a file exists at the provided |path|.
//
// Returns an OK status if the file definitely exists. An OK status does not
// indicate that attempts to read or write the file will succeed.
// Returns IREE_STATUS_NOT_FOUND if the file does not exist.
iree_status_t iree_file_exists(const char* path);

// Synchronously reads a file's contents into memory.
//
// Returns the contents of the file in |out_contents|.
// |allocator| is used to allocate the memory and the caller must use the same
// allocator when freeing it.
iree_status_t iree_file_read_contents(const char* path,
                                      iree_allocator_t allocator,
                                      iree_byte_span_t* out_contents);

// Synchronously writes a byte buffer into a file.
// Existing contents are overwritten.
iree_status_t iree_file_write_contents(const char* path,
                                       iree_const_byte_span_t content);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // IREE_BASE_INTERNAL_FILE_IO_H_
