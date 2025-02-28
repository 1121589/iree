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

#include "iree/compiler/Conversion/HLOToLinalg/Passes.h"

#include "iree/compiler/Conversion/HLOToHLO/Passes.h"
#include "iree/compiler/Conversion/HLOToLinalg/HLOToLinalgOnTensorPasses.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Transforms/Passes.h"

namespace mlir {
namespace iree_compiler {

void addHLOToLinalgOnBuffersPasses(OpPassManager &pm) {
  // HACK: this is only for the old linalg-on-buffers path. It should be
  // deleted.
  pm.addNestedPass<FuncOp>(createHLOToLinalgOnTensorsPass(false));
  pm.addNestedPass<FuncOp>(createConvertElementwiseToLinalgPass());
  pm.addNestedPass<FuncOp>(createLinalgFoldUnitExtentDimsPass());
  pm.addNestedPass<FuncOp>(createCanonicalizerPass());
  pm.addNestedPass<FuncOp>(createFusionOfTensorOpsPass());
  pm.addNestedPass<FuncOp>(createCanonicalizerPass());
  pm.addNestedPass<FuncOp>(createCSEPass());
  pm.addNestedPass<FuncOp>(createHLOToLinalgOnBuffersPass());
}

static PassPipelineRegistration<> hloToLinalgOnBuffersPipeline(
    "iree-codegen-hlo-to-linalg-pipeline",
    "Runs the progressive lowering pipeline from XLA HLO to Linalg on buffers",
    [](OpPassManager &passManager) {
      addHLOToLinalgOnBuffersPasses(passManager);
    });

}  // namespace iree_compiler
}  // namespace mlir
