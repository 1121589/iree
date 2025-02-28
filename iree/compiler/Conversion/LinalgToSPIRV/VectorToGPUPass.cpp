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

//===---- VectorToGPUPass.cpp - Pass for the final SPIR-V conversion ------===//
//
// This file implement a pass to convert vector dialect operations to GPU
// operations distributed across a subgroup.
//
//===----------------------------------------------------------------------===//
#include <memory>

#include "iree/compiler/Conversion/CodegenUtils/FunctionUtils.h"
#include "iree/compiler/Conversion/CodegenUtils/MarkerUtils.h"
#include "iree/compiler/Conversion/CodegenUtils/TransformUtils.h"
#include "iree/compiler/Conversion/LinalgToSPIRV/Passes.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/FormatVariadic.h"
#include "mlir/Dialect/GPU/GPUDialect.h"
#include "mlir/Dialect/Linalg/IR/LinalgOps.h"
#include "mlir/Dialect/Linalg/Transforms/CodegenStrategy.h"
#include "mlir/Dialect/Linalg/Transforms/Transforms.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/Dialect/SCF/SCF.h"
#include "mlir/Dialect/SPIRV/IR/TargetAndABI.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/Dialect/Vector/VectorOps.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Support/LLVM.h"
#include "mlir/Support/LogicalResult.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Transforms/GreedyPatternRewriteDriver.h"

namespace mlir {
namespace iree_compiler {
namespace {

struct ConvertVectorToGPUPass
    : public PassWrapper<ConvertVectorToGPUPass, OperationPass<FuncOp>> {
  void getDependentDialects(DialectRegistry &registry) const override {
    registry.insert<AffineDialect, gpu::GPUDialect, memref::MemRefDialect,
                    scf::SCFDialect, vector::VectorDialect>();
  }

  void runOnOperation() override;

 private:
  void tileAndVectorizeLinalgCopy(FuncOp funcOp, MLIRContext *context);
  void lowerVectorOps(FuncOp funcOp, MLIRContext *context);
};

class VectorToGPUConversionTarget : public ConversionTarget {
 public:
  using ConversionTarget::ConversionTarget;

 protected:
  // Standard operation are legal if they operate on scalars. We need to
  // legalize operations on vectors.
  bool isDynamicallyLegal(Operation *op) const override {
    auto isVectorType = [](Type t) { return t.isa<VectorType>(); };
    if (llvm::any_of(op->getResultTypes(), isVectorType) ||
        llvm::any_of(op->getOperandTypes(), isVectorType))
      return false;
    return true;
  }
};

void ConvertVectorToGPUPass::tileAndVectorizeLinalgCopy(FuncOp funcOp,
                                                        MLIRContext *context) {
  // 1. Tile linalg and distribute it on invocations.
  std::unique_ptr<ConversionTarget> target =
      std::make_unique<ConversionTarget>(*context);
  target->addDynamicallyLegalOp<linalg::CopyOp>([&](linalg::CopyOp copy) {
    return !(hasMarker(copy, getCopyToWorkgroupMemoryMarker()));
  });
  target->markUnknownOpDynamicallyLegal([](Operation *) { return true; });
  OwningRewritePatternList tileAndDistributePattern(&getContext());
  populateLinalgTileAndDistributePatterns(context, tileAndDistributePattern);
  if (failed(applyPartialConversion(funcOp, *target,
                                    std::move(tileAndDistributePattern)))) {
    return signalPassFailure();
  }

  // 2. Canonicalize the IR generated by tiling.
  OwningRewritePatternList canonicalizePatterns =
      linalg::getLinalgTilingCanonicalizationPatterns(context);
  canonicalizePatterns.insert<AffineMinCanonicalizationPattern,
                              linalg::AffineMinSCFCanonicalizationPattern>(
      context);
  (void)applyPatternsAndFoldGreedily(funcOp, std::move(canonicalizePatterns));

  // 3. Vectorize the tiled linalg to be able to map it to load/store vector.
  OwningRewritePatternList vectorizationPatterns(&getContext());
  linalg::insertVectorizationPatterns<linalg::CopyOp>(
      vectorizationPatterns, linalg::LinalgVectorizationOptions(),
      linalg::LinalgTransformationFilter(
          Identifier::get(getVectorizeMarker(), context), {}));
  (void)applyPatternsAndFoldGreedily(funcOp, std::move(vectorizationPatterns));
}

void ConvertVectorToGPUPass::runOnOperation() {
  MLIRContext *context = &getContext();
  FuncOp funcOp = getOperation();
  tileAndVectorizeLinalgCopy(funcOp, context);
}
}  // namespace

//===----------------------------------------------------------------------===//
// Pass entry point and registration
//===----------------------------------------------------------------------===//
std::unique_ptr<OperationPass<FuncOp>> createVectorToGPUPass() {
  return std::make_unique<ConvertVectorToGPUPass>();
}

static PassRegistration<ConvertVectorToGPUPass> pass(
    "iree-codegen-vector-to-gpu",
    "Convert vector dialect to gpu subgroup level GPU instructions");
}  // namespace iree_compiler
}  // namespace mlir
