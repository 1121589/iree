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

//===- Passes.cpp - Pipeline from HLO to Linalg to SPIR-V -----------------===//
//
// Implementation of conversion from XLA-HLO to Linalg to SPIR-V dialect.
//
//===----------------------------------------------------------------------===//

#include "iree/compiler/Conversion/LinalgToSPIRV/Passes.h"

#include "iree/compiler/Conversion/Common/Passes.h"
#include "iree/compiler/Conversion/HLOToHLO/Passes.h"
#include "iree/compiler/Conversion/HLOToLinalg/Passes.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/CodeGenOptionUtils.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/MemorySpace.h"
#include "iree/compiler/Conversion/LinalgToVector/Passes.h"
#include "iree/compiler/Dialect/Shape/Transforms/Passes.h"
#include "llvm/Support/CommandLine.h"
#include "mlir/Conversion/GPUToSPIRV/GPUToSPIRV.h"
#include "mlir/Conversion/SCFToGPU/SCFToGPUPass.h"
#include "mlir/Conversion/StandardToSPIRV/StandardToSPIRV.h"
#include "mlir/Conversion/StandardToSPIRV/StandardToSPIRVPass.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/GPU/Passes.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/Linalg/Passes.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/MemRef/Transforms/Passes.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/SPIRV/IR/SPIRVOps.h"
#include "mlir/Dialect/SPIRV/Transforms/Passes.h"
#include "mlir/Dialect/SPIRV/Transforms/SPIRVConversion.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/Pass/PassManager.h"
#include "mlir/Pass/PassOptions.h"
#include "mlir/Pass/PassRegistry.h"
#include "mlir/Transforms/FoldUtils.h"
#include "mlir/Transforms/Passes.h"

namespace mlir {
namespace iree_compiler {

static void addLinalgToSPIRVPasses(OpPassManager &pm,
                                   const SPIRVCodegenOptions &options) {
  //===--------------------------------------------------------------------===//
  // Initial clean up.
  //===--------------------------------------------------------------------===//
  pm.nest<ModuleOp>().addPass(createCanonicalizerPass());
  pm.nest<ModuleOp>().addPass(createCSEPass());

  //===--------------------------------------------------------------------===//
  // Tile Linalg on buffers.
  //
  // Pre-conditions:
  //   - All Linalg ops have buffer semantics.
  //
  // Post-conditions:
  //   - If there are multiple linalg operations in the dispatch region, they
  //     are fused, using tile+fuse approach.
  //     - The fused loops are distributed across workgroups.
  //   - The operations that cannot be fused at buffer levels are split into
  //     separate entry points.
  //   - If there is a single linalg operation in the dispatch region, it is
  //     tiled and the generated parallel loop distributed.
  //     - The tiled linalg operation can be tiled again one or more times and
  //       then vectorized.
  //   - Otherwise:
  //     - The Linalg op is kept untouched.
  //
  //===--------------------------------------------------------------------===//

  // flow.dispatch.workgroups performed abstract tiling and distribution. Make
  // them concrete now since we know the target and settings now.
  pm.addPass(createConcretizeTileAmongWorkgroupsPass(options));

  pm.addPass(createTileAndVectorizeInOneWorkgroupPass(options));
  pm.nest<ModuleOp>().addPass(createCanonicalizerPass());

  //===--------------------------------------------------------------------===//
  // Map to GPU processor IDs.
  //
  // Post-conditions:
  //   - loop.parallel ops are converted to loop.for ops and mapped to
  //     workgroups.
  //   - Linalg ops are converted to loop.for ops and mapped to workitems.
  //===--------------------------------------------------------------------===//
  pm.addPass(createConvertToGPUPass());
  pm.nest<ModuleOp>().addNestedPass<FuncOp>(createVectorToGPUPass());
  pm.nest<ModuleOp>().addPass(createLowerAffinePass());
  pm.nest<ModuleOp>().addPass(createCanonicalizerPass());
  pm.nest<ModuleOp>().addPass(createCSEPass());

  pm.nest<ModuleOp>().addNestedPass<FuncOp>(createResolveShapeOpsPass());

  //===--------------------------------------------------------------------===//
  // Prepare stdandard ops for SPIR-V conversion.
  //
  // Post-conditions:
  //   - Load/store on std.subview ops are converted into load/store on the
  //     original buffers.
  //===--------------------------------------------------------------------===//
  pm.nest<ModuleOp>().addNestedPass<FuncOp>(
      createVectorTransferOptimizationPass());
  pm.nest<ModuleOp>().addPass(memref::createFoldSubViewOpsPass());
  pm.nest<ModuleOp>().addPass(createCanonicalizerPass());
  pm.nest<ModuleOp>().addPass(createCSEPass());
  pm.nest<ModuleOp>().addPass(createVectorizeMemrefLoadStorePass());
  pm.nest<ModuleOp>().addNestedPass<FuncOp>(
      createConvertVectorToCooperativeMatrixPass());
  pm.nest<ModuleOp>().addNestedPass<FuncOp>(createForOpCanonicalizationPass());
  pm.nest<ModuleOp>().addPass(createCanonicalizerPass());
  pm.nest<ModuleOp>().addPass(createCSEPass());

  pm.nest<ModuleOp>().addPass(createFlattenMemRefSubspanPass());
  pm.nest<ModuleOp>().addPass(createLowerAffinePass());
  pm.nest<ModuleOp>().addPass(createCanonicalizerPass());
  pm.nest<ModuleOp>().addPass(createCSEPass());

  //===--------------------------------------------------------------------===//
  // Final conversion to SPIR-V dialect.
  //
  // Post-conditions:
  //   - All ops are converted to SPIR-V counterparts.
  //   - spv.module ops are formed to hold all SPIR-V ops.
  //===--------------------------------------------------------------------===//
  pm.nest<ModuleOp>().addPass(createConvertToSPIRVPass());

  //===--------------------------------------------------------------------===//
  // SPIR-V dialect level conversions.
  //
  // Post-conditions:
  //   - SPIR-V Entry point ops are inserted.
  //   - Required version/extension/capability are deduced.
  //===--------------------------------------------------------------------===//
  OpPassManager &spirvModulePM = pm.nest<ModuleOp>().nest<spirv::ModuleOp>();
  spirvModulePM.addPass(spirv::createLowerABIAttributesPass());
  spirvModulePM.addPass(createCanonicalizerPass());
  spirvModulePM.addPass(createCSEPass());
  spirvModulePM.addPass(spirv::createUpdateVersionCapabilityExtensionPass());
}

void buildSPIRVTransformPassPipeline(OpPassManager &pm,
                                     const SPIRVCodegenOptions &options) {
  //===--------------------------------------------------------------------===//
  // Inline the impl dispatch function into the wrapper dispatch function.
  //
  // TODO(antiagainst): re-evaluate the inlining timing.
  //===--------------------------------------------------------------------===//
  pm.nest<ModuleOp>().addPass(createInlinerPass());
  pm.nest<ModuleOp>().addNestedPass<FuncOp>(createBufferAllocViewCleanUpPass());
  WorkgroupMemoryAllocationFn allocationFn =
      [](OpBuilder &builder, Location loc, ArrayRef<int64_t> staticShape,
         Type elementType, ArrayRef<Value> dynamicSizes) {
        MemRefType allocType = MemRefType::get(staticShape, elementType, {},
                                               getWorkgroupMemorySpace());
        return builder.create<memref::AllocOp>(loc, allocType, dynamicSizes);
      };
  addLinalgBufferizePasses(pm.nest<ModuleOp>(), allocationFn);

  //===--------------------------------------------------------------------===//
  // Convert Linalg ops to SPIR-V ops.
  //
  // Post-conditions:
  //   - All Linalg/Loops/GPU/Affine/Standard ops are converted away.
  //   - The module contains the final spv.module ready for serialization.
  //===--------------------------------------------------------------------===//
  addLinalgToSPIRVPasses(pm, options);
}

static PassPipelineRegistration<> linalgToSPIRVPipeline(
    "iree-codegen-linalg-to-spirv-pipeline",
    "Runs the progressive lowering pipeline from Linalg to SPIR-V",
    [](OpPassManager &passManager) {
      addLinalgToSPIRVPasses(passManager,
                             getSPIRVCodegenOptionsFromClOptions());
    });

static PassPipelineRegistration<> hloToLinalgSPIRVPipeline(
    "iree-codegen-hlo-to-spirv-pipeline",
    "Runs the progressive lowering pipeline from XLA HLO to Linalg to "
    "SPIR-V",
    [](OpPassManager &passManager) {
      buildSPIRVTransformPassPipeline(passManager,
                                      getSPIRVCodegenOptionsFromClOptions());
    });

}  // namespace iree_compiler
}  // namespace mlir
