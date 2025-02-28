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

#include "iree/compiler/Dialect/HAL/IR/HALOps.h"
#include "iree/compiler/Dialect/HAL/Utils/TypeUtils.h"
#include "iree/compiler/Dialect/VM/Conversion/ImportUtils.h"
#include "mlir/Transforms/DialectConversion.h"

namespace mlir {
namespace iree_compiler {

void populateHALBufferViewToVMPatterns(MLIRContext *context,
                                       SymbolTable &importSymbols,
                                       TypeConverter &typeConverter,
                                       OwningRewritePatternList &patterns) {
  patterns.insert<VMImportOpConversion<IREE::HAL::BufferViewCreateOp>>(
      context, importSymbols, typeConverter, "hal.buffer_view.create");
  patterns.insert<VMImportOpConversion<IREE::HAL::BufferViewBufferOp>>(
      context, importSymbols, typeConverter, "hal.buffer_view.buffer");
  patterns.insert<VMImportOpConversion<IREE::HAL::BufferViewByteLengthOp>>(
      context, importSymbols, typeConverter, "hal.buffer_view.byte_length");
  patterns.insert<VMImportOpConversion<IREE::HAL::BufferViewElementTypeOp>>(
      context, importSymbols, typeConverter, "hal.buffer_view.element_type");
  patterns.insert<VMImportOpConversion<IREE::HAL::BufferViewRankOp>>(
      context, importSymbols, typeConverter, "hal.buffer_view.rank");
  patterns.insert<VMImportOpConversion<IREE::HAL::BufferViewDimOp>>(
      context, importSymbols, typeConverter, "hal.buffer_view.dim");
  patterns.insert<VMImportOpConversion<IREE::HAL::BufferViewTraceOp>>(
      context, importSymbols, typeConverter, "hal.buffer_view.trace");
}

}  // namespace iree_compiler
}  // namespace mlir
