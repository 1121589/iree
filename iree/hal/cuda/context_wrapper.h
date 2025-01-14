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

#ifndef IREE_HAL_CUDA_CONTEXT_WRAPPER_H_
#define IREE_HAL_CUDA_CONTEXT_WRAPPER_H_

#include "iree/hal/api.h"
#include "iree/hal/cuda/cuda_headers.h"
#include "iree/hal/cuda/dynamic_symbols.h"

// Structure to wrap all objects constant within a context. This makes it
// simpler to pass it to the different objects and saves memory.
typedef struct {
  CUcontext cu_context;
  iree_allocator_t host_allocator;
  iree_hal_cuda_dynamic_symbols_t* syms;
} iree_hal_cuda_context_wrapper_t;

#endif  // IREE_HAL_CUDA_CONTEXT_WRAPPER_H_
