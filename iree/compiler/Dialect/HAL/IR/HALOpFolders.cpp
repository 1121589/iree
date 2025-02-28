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
#include "iree/compiler/Dialect/IREE/IR/IREEOps.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/StringExtras.h"
#include "mlir/Dialect/StandardOps/IR/Ops.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Matchers.h"
#include "mlir/IR/OpDefinition.h"
#include "mlir/IR/OpImplementation.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/SymbolTable.h"
#include "mlir/Support/LogicalResult.h"

namespace mlir {
namespace iree_compiler {
namespace IREE {
namespace HAL {

//===----------------------------------------------------------------------===//
// hal.tensor.cast
//===----------------------------------------------------------------------===//

OpFoldResult TensorCastOp::fold(ArrayRef<Attribute> operands) {
  if (source().getType() == target().getType()) {
    return source();
  }
  return {};
}

//===----------------------------------------------------------------------===//
// hal.variable.*
//===----------------------------------------------------------------------===//

namespace {

/// Converts variable initializer functions that evaluate to a constant to a
/// specified initial value.
struct InlineConstVariableOpInitializer : public OpRewritePattern<VariableOp> {
  using OpRewritePattern<VariableOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(VariableOp op,
                                PatternRewriter &rewriter) const override {
    if (!op.initializer()) return failure();
    auto *symbolOp =
        SymbolTable::lookupNearestSymbolFrom(op, op.initializer().getValue());
    auto initializer = cast<FuncOp>(symbolOp);
    if (initializer.getBlocks().size() == 1 &&
        initializer.getBlocks().front().getOperations().size() == 2 &&
        isa<mlir::ReturnOp>(
            initializer.getBlocks().front().getOperations().back())) {
      auto &primaryOp = initializer.getBlocks().front().getOperations().front();
      Attribute constResult;
      if (matchPattern(primaryOp.getResult(0), m_Constant(&constResult))) {
        auto newOp = rewriter.create<VariableOp>(op.getLoc(), op.sym_name(),
                                                 op.is_mutable(), op.type(),
                                                 constResult);
        newOp.setVisibility(op.getVisibility());
        rewriter.replaceOp(op, {});
        return success();
      }
    }
    return failure();
  }
};

}  // namespace

void VariableOp::getCanonicalizationPatterns(OwningRewritePatternList &results,
                                             MLIRContext *context) {
  results.insert<InlineConstVariableOpInitializer>(context);
}

namespace {

class PropagateVariableLoadAddress
    : public OpRewritePattern<VariableLoadIndirectOp> {
  using OpRewritePattern::OpRewritePattern;

 public:
  LogicalResult matchAndRewrite(VariableLoadIndirectOp op,
                                PatternRewriter &rewriter) const override {
    if (auto addressOp = dyn_cast_or_null<VariableAddressOp>(
            op.variable().getDefiningOp())) {
      rewriter.replaceOpWithNewOp<VariableLoadOp>(op, op.result().getType(),
                                                  addressOp.variable());
      return success();
    }
    return failure();
  }
};

}  // namespace

void VariableLoadIndirectOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<PropagateVariableLoadAddress>(context);
}

namespace {

/// Erases hal.variable.store ops that are no-ops.
/// This can happen if there was a variable load, some DCE'd usage, and a
/// store back to the same variable: we want to be able to elide the entire load
/// and store.
struct EraseUnusedVariableStoreOp : public OpRewritePattern<VariableStoreOp> {
  using OpRewritePattern<VariableStoreOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(VariableStoreOp op,
                                PatternRewriter &rewriter) const override {
    if (auto loadOp =
            dyn_cast_or_null<VariableLoadOp>(op.value().getDefiningOp())) {
      if (loadOp.variable() == op.variable()) {
        rewriter.eraseOp(op);
        return success();
      }
    }
    return failure();
  }
};

}  // namespace

void VariableStoreOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<EraseUnusedVariableStoreOp>(context);
}

namespace {

class PropagateVariableStoreAddress
    : public OpRewritePattern<VariableStoreIndirectOp> {
  using OpRewritePattern::OpRewritePattern;

 public:
  LogicalResult matchAndRewrite(VariableStoreIndirectOp op,
                                PatternRewriter &rewriter) const override {
    if (auto addressOp = dyn_cast_or_null<VariableAddressOp>(
            op.variable().getDefiningOp())) {
      rewriter.replaceOpWithNewOp<VariableStoreOp>(op, op.value(),
                                                   addressOp.variable());
      return success();
    }
    return failure();
  }
};

}  // namespace

void VariableStoreIndirectOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<PropagateVariableStoreAddress>(context);
}

//===----------------------------------------------------------------------===//
// hal.allocator.*
//===----------------------------------------------------------------------===//

// Computes the element count of a possibly-dynamic shaped tensor.
static Value getElementCount(Location loc, Value baseValue,
                             ValueRange shapeDims, OpBuilder &builder) {
  Value value = baseValue;
  for (auto dim : shapeDims) {
    value = builder.createOrFold<mlir::MulIOp>(loc, value, dim);
  }
  return value;
}

namespace {

/// Expands hal.allocator.compute_size to IR performing the math.
struct ExpandAllocatorComputeSizeOp
    : public OpRewritePattern<AllocatorComputeSizeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(AllocatorComputeSizeOp op,
                                PatternRewriter &rewriter) const override {
    // TODO(benvanik): use buffer constraints for alignment.
    BufferConstraintsAdaptor bufferConstraints(op.getLoc(), op.allocator());

    auto elementSize =
        getElementByteCount(op.getLoc(), op.element_type(), rewriter);
    auto byteSize =
        getElementCount(op.getLoc(), elementSize, op.shape(), rewriter);

    rewriter.replaceOp(op, {byteSize});
    return success();
  }
};

}  // namespace

void AllocatorComputeSizeOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ExpandAllocatorComputeSizeOp>(context);
}

namespace {

/// Expands hal.allocator.compute_offset to IR performing the math.
struct ExpandAllocatorComputeOffsetOp
    : public OpRewritePattern<AllocatorComputeOffsetOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(AllocatorComputeOffsetOp op,
                                PatternRewriter &rewriter) const override {
    // TODO(benvanik): use buffer constraints.
    BufferConstraintsAdaptor bufferConstraints(op.getLoc(), op.allocator());

    auto offset = rewriter.createOrFold<mlir::ConstantIndexOp>(op.getLoc(), 0);
    for (size_t i = 0; i < op.indices().size(); ++i) {
      // TODO(benvanik): check error case in debug builds.
      // if (indices[i] >= shape[i]) {
      //   return iree_make_status(IREE_STATUS_OUT_OF_RANGE,
      //                           "index[%zu] out of bounds: %d >= %d", i,
      //                           indices[i], shape[i]);
      // }
      auto axisOffset = op.indices()[i];
      for (size_t j = i + 1; j < op.shape().size(); ++j) {
        axisOffset = rewriter.createOrFold<mlir::MulIOp>(
            op.getLoc(), axisOffset, op.shape()[j]);
      }
      offset =
          rewriter.createOrFold<mlir::AddIOp>(op.getLoc(), offset, axisOffset);
    }
    auto elementSize =
        getElementByteCount(op.getLoc(), op.element_type(), rewriter);
    auto byteOffset =
        rewriter.createOrFold<mlir::MulIOp>(op.getLoc(), offset, elementSize);

    rewriter.replaceOp(op, {byteOffset});
    return success();
  }
};

}  // namespace

void AllocatorComputeOffsetOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ExpandAllocatorComputeOffsetOp>(context);
}

namespace {

/// Expands hal.allocator.compute_range to IR performing the math.
struct ExpandAllocatorComputeRangeOp
    : public OpRewritePattern<AllocatorComputeRangeOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(AllocatorComputeRangeOp op,
                                PatternRewriter &rewriter) const override {
    // TODO(benvanik): use buffer constraints.
    BufferConstraintsAdaptor bufferConstraints(op.getLoc(), op.allocator());

    SmallVector<Value, 6> endIndices(op.shape().size());
    auto one = rewriter.createOrFold<mlir::ConstantIndexOp>(op.getLoc(), 1);
    for (size_t i = 0; i < endIndices.size(); ++i) {
      endIndices[i] = rewriter.createOrFold<mlir::SubIOp>(
          op.getLoc(),
          rewriter.createOrFold<mlir::AddIOp>(op.getLoc(), op.indices()[i],
                                              op.lengths()[i]),
          one);
    }

    auto startByteOffset = rewriter.createOrFold<AllocatorComputeOffsetOp>(
        op.getLoc(), rewriter.getIndexType(), op.allocator(), op.shape(),
        op.element_type(), op.indices());
    auto endByteOffset = rewriter.createOrFold<AllocatorComputeOffsetOp>(
        op.getLoc(), rewriter.getIndexType(), op.allocator(), op.shape(),
        op.element_type(), endIndices);

    auto elementSize =
        getElementByteCount(op.getLoc(), op.element_type(), rewriter);
    auto offsetLength = rewriter.createOrFold<mlir::AddIOp>(
        op.getLoc(),
        rewriter.createOrFold<mlir::SubIOp>(op.getLoc(), endByteOffset,
                                            startByteOffset),
        elementSize);

    rewriter.replaceOp(op, {startByteOffset, offsetLength});
    return success();
  }
};

}  // namespace

void AllocatorComputeRangeOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ExpandAllocatorComputeRangeOp>(context);
}

namespace {

/// Expands hal.allocator.allocate.constant to an allocation and data write.
struct ExpandAllocatorConstantOp
    : public OpRewritePattern<AllocatorConstantOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(AllocatorConstantOp op,
                                PatternRewriter &rewriter) const override {
    auto shapedType = op.value().getType();
    auto elementType =
        IREE::HAL::getElementTypeValue(shapedType.getElementType());
    if (!elementType.hasValue()) {
      return rewriter.notifyMatchFailure(op, "unhandled element type");
    }

    // TODO(benvanik): compute from SSA use-def chain uses.
    IREE::HAL::MemoryTypeBitfield memoryTypes =
        IREE::HAL::MemoryTypeBitfield::DeviceLocal |
        IREE::HAL::MemoryTypeBitfield::HostVisible;
    IREE::HAL::BufferUsageBitfield bufferUsage =
        IREE::HAL::BufferUsageBitfield::All |
        IREE::HAL::BufferUsageBitfield::Constant;
    Type bufferType = IREE::HAL::BufferType::get(rewriter.getContext());

    auto hostBuffer = rewriter.createOrFold<IREE::ByteBufferConstantOp>(
        op.getLoc(), IREE::ByteBufferType::get(rewriter.getContext()),
        op.value());
    auto zero = rewriter.createOrFold<mlir::ConstantIndexOp>(op.getLoc(), 0);
    auto neg1 = rewriter.createOrFold<mlir::ConstantIndexOp>(op.getLoc(), -1);
    auto deviceBuffer = rewriter.createOrFold<AllocatorMapOp>(
        op.getLoc(), bufferType, op.allocator(), memoryTypes, bufferUsage,
        hostBuffer, zero, neg1);

    if (op.result().getType().isa<IREE::HAL::BufferViewType>()) {
      // Wrap in a buffer view.
      SmallVector<Value, 4> shape;
      if (shapedType.getRank() >= 1) {
        for (auto dim : shapedType.getShape()) {
          shape.push_back(
              rewriter.createOrFold<mlir::ConstantIndexOp>(op.getLoc(), dim));
        }
      }
      auto bufferView = rewriter.createOrFold<BufferViewCreateOp>(
          op.getLoc(), deviceBuffer, elementType.getValue(), shape);
      rewriter.replaceOp(op, {bufferView});
    } else {
      rewriter.replaceOp(op, {deviceBuffer});
    }
    return success();
  }
};

}  // namespace

void AllocatorConstantOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ExpandAllocatorConstantOp>(context);
}

LogicalResult AllocatorPackOp::fold(ArrayRef<Attribute> operands,
                                    SmallVectorImpl<OpFoldResult> &results) {
  Builder builder(getContext());

  // If there are no slices then the entire pack results in a zero-length slab.
  if (packed_offsets().empty()) {
    results.push_back(builder.getZeroAttr(builder.getIndexType()));
    return success();
  }

  // If there's a single slice then we just use that as there is no packing to
  // perform.
  if (packed_offsets().size() == 1) {
    // Total length is the slice size and offset is always either 0 or the
    // provided optional base offset.
    results.push_back(dynamic_slice_sizes()[0]);
    if (offset()) {
      results.push_back(offset());
    } else {
      results.push_back(builder.getZeroAttr(builder.getIndexType()));
    }
    return success();
  }

  return failure();
}

namespace {

/// Propagates base offsets on a pack op to its results.
/// This allows for better folding of the results after packing has completed.
/// The offset value is just a convenience for when splitting pack ops and has
/// no impact on the actual packing operation.
struct PropagateAllocatorPackBaseOffset
    : public OpRewritePattern<AllocatorPackOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(AllocatorPackOp op,
                                PatternRewriter &rewriter) const override {
    // Offset is optional.
    auto baseOffset = op.offset();
    if (!baseOffset) return failure();

    // We always strip the offset here.
    rewriter.updateRootInPlace(op, [&]() { op.offsetMutable().clear(); });

    // Zero offsets don't do anything and can just be removed so we can avoid
    // inserting a bunch of additional IR.
    if (auto constantOp =
            dyn_cast_or_null<ConstantIndexOp>(baseOffset.getDefiningOp())) {
      if (constantOp.getValue() == 0) {
        return success();
      }
    }

    // Propagate the offset to all returned slice offsets.
    rewriter.setInsertionPointAfter(op);
    for (auto sliceOffset : op.packed_offsets()) {
      auto addOp =
          rewriter.create<mlir::AddIOp>(op.getLoc(), baseOffset, sliceOffset);
      SmallPtrSet<Operation *, 1> exclusions;
      exclusions.insert(addOp);
      sliceOffset.replaceAllUsesExcept(addOp.result(), exclusions);
    }

    return success();
  }
};

/// Sorts and compacts the slice intervals into a dense ascending order set.
/// This is not required by the packing algorithm but yields more
/// consistent-looking IR and makes the range overlaps easier to see for us
/// meatbags.
struct CanonicalizeAllocatorPackIntervals
    : public OpRewritePattern<AllocatorPackOp> {
  using OpRewritePattern::OpRewritePattern;
  LogicalResult matchAndRewrite(AllocatorPackOp op,
                                PatternRewriter &rewriter) const override {
    // Get the slices in a possibly unsorted order and sort.
    auto slices = op.getSlices();
    std::stable_sort(slices.begin(), slices.end());

    // See if the sorted order is different than how they are stored in the op.
    bool orderChanged = false;
    for (auto it : llvm::zip(slices, op.packed_offsets())) {
      if (std::get<0>(it).packedOffset != std::get<1>(it)) {
        orderChanged = true;
        break;
      }
    }
    if (!orderChanged) return failure();

    // TODO(benvanik): compact the slice ranges.

    // Rebuild the op with the sorted values.
    SmallVector<int64_t> lifetimeIntervals(slices.size() * 2);
    SmallVector<Value> dynamicSliceSizes(slices.size());
    for (size_t i = 0; i < slices.size(); ++i) {
      const auto &slice = slices[i];
      lifetimeIntervals[2 * i + 0] = slice.lifetimeStart;
      lifetimeIntervals[2 * i + 1] = slice.lifetimeEnd;
      dynamicSliceSizes[i] = slice.dynamicSize;
    }
    SmallVector<Type> packedOffsetTypes(slices.size(), rewriter.getIndexType());
    auto newOp = rewriter.create<AllocatorPackOp>(
        op.getLoc(), op.total_length().getType(), packedOffsetTypes,
        op.allocator(), op.offset(),
        rewriter.getIndexArrayAttr(lifetimeIntervals), dynamicSliceSizes);

    // Remap existing values to the new values.
    op.total_length().replaceAllUsesWith(newOp.total_length());
    for (size_t i = 0; i < newOp.packed_offsets().size(); ++i) {
      slices[i].packedOffset.replaceAllUsesWith(newOp.packed_offsets()[i]);
    }

    rewriter.eraseOp(op);
    return success();
  }
};

}  // namespace

void AllocatorPackOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<PropagateAllocatorPackBaseOffset>(context);
  results.insert<CanonicalizeAllocatorPackIntervals>(context);
}

//===----------------------------------------------------------------------===//
// hal.buffer.*
//===----------------------------------------------------------------------===//

namespace {

/// Skips a hal.buffer.allocator accessor when the buffer view was created in
/// the same scope and we know the origin buffer.
struct SkipBufferAllocatorOp : public OpRewritePattern<BufferAllocatorOp> {
  using OpRewritePattern<BufferAllocatorOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(BufferAllocatorOp op,
                                PatternRewriter &rewriter) const override {
    if (auto allocateOp = dyn_cast_or_null<AllocatorAllocateOp>(
            op.buffer().getDefiningOp())) {
      rewriter.replaceOp(op, allocateOp.allocator());
      return success();
    } else if (auto allocateOp = dyn_cast_or_null<AllocatorConstantOp>(
                   op.buffer().getDefiningOp())) {
      rewriter.replaceOp(op, allocateOp.allocator());
      return success();
    } else if (auto subspanOp = dyn_cast_or_null<BufferSubspanOp>(
                   op.buffer().getDefiningOp())) {
      rewriter.replaceOpWithNewOp<BufferAllocatorOp>(op, op.result().getType(),
                                                     subspanOp.source_buffer());
      return success();
    }
    return failure();
  }
};

}  // namespace

void BufferAllocatorOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<SkipBufferAllocatorOp>(context);
}

//===----------------------------------------------------------------------===//
// hal.buffer_view.*
//===----------------------------------------------------------------------===//

namespace {

/// Expands a hal.buffer_view.subview op into range computation and creation
/// ops. This allows for greater opportunity to CSE/bypass/etc the buffer view
/// operations.
struct ExpandBufferViewSubviewOp
    : public OpRewritePattern<BufferViewSubviewOp> {
  using OpRewritePattern<BufferViewSubviewOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(BufferViewSubviewOp op,
                                PatternRewriter &rewriter) const override {
    auto computeRangeOp = rewriter.create<BufferViewComputeRangeOp>(
        op.getLoc(), op.buffer_view(), op.indices(), op.lengths());

    auto bufferValue = rewriter.createOrFold<BufferViewBufferOp>(
        op.getLoc(), IREE::HAL::BufferType::get(rewriter.getContext()),
        op.buffer_view());
    auto subspanValue = rewriter.createOrFold<BufferSubspanOp>(
        op.getLoc(), bufferValue.getType(), bufferValue,
        computeRangeOp.offset(), computeRangeOp.length());

    auto elementTypeValue = rewriter.createOrFold<BufferViewElementTypeOp>(
        op.getLoc(), rewriter.getI32Type(), op.buffer_view());
    rewriter.replaceOpWithNewOp<BufferViewCreateOp>(
        op, subspanValue, elementTypeValue, op.lengths());
    return success();
  }
};

}  // namespace

void BufferViewSubviewOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ExpandBufferViewSubviewOp>(context);
}

namespace {

/// Skips a hal.buffer_view.buffer accessor when the buffer view was created in
/// the same scope and we know the origin buffer.
struct SkipBufferViewBufferOp : public OpRewritePattern<BufferViewBufferOp> {
  using OpRewritePattern<BufferViewBufferOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(BufferViewBufferOp op,
                                PatternRewriter &rewriter) const override {
    if (auto createOp = dyn_cast_or_null<BufferViewCreateOp>(
            op.buffer_view().getDefiningOp())) {
      rewriter.replaceOp(op, createOp.buffer());
      return success();
    }
    return failure();
  }
};

}  // namespace

void BufferViewBufferOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<SkipBufferViewBufferOp>(context);
}

namespace {

/// Expands a hal.buffer_view.compute_offset op to use
/// hal.allocator.compute_offset. This allows for all of the shape math to
/// happen in the VM where we can better optimize it.
struct ExpandBufferViewComputeOffsetOp
    : public OpRewritePattern<BufferViewComputeOffsetOp> {
  using OpRewritePattern<BufferViewComputeOffsetOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(BufferViewComputeOffsetOp op,
                                PatternRewriter &rewriter) const override {
    auto bufferValue = rewriter.createOrFold<BufferViewBufferOp>(
        op.getLoc(), IREE::HAL::BufferType::get(rewriter.getContext()),
        op.buffer_view());
    auto allocatorValue = rewriter.createOrFold<BufferAllocatorOp>(
        op.getLoc(), AllocatorType::get(rewriter.getContext()), bufferValue);
    int rank = op.indices().size();
    SmallVector<Type, 4> dimTypes(rank, rewriter.getIndexType());
    auto dimsOp = rewriter.create<BufferViewDimsOp>(op.getLoc(), dimTypes,
                                                    op.buffer_view());
    auto elementTypeValue = rewriter.createOrFold<BufferViewElementTypeOp>(
        op.getLoc(), rewriter.getI32Type(), op.buffer_view());
    rewriter.replaceOpWithNewOp<AllocatorComputeOffsetOp>(
        op, allocatorValue, dimsOp.result(), elementTypeValue, op.indices());
    return success();
  }
};

}  // namespace

void BufferViewComputeOffsetOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ExpandBufferViewComputeOffsetOp>(context);
}

namespace {

/// Expands a hal.buffer_view.compute_range op to use
/// hal.allocator.compute_range. This allows for all of the shape math to
/// happen in the VM where we can better optimize it.
struct ExpandBufferViewComputeRangeOp
    : public OpRewritePattern<BufferViewComputeRangeOp> {
  using OpRewritePattern<BufferViewComputeRangeOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(BufferViewComputeRangeOp op,
                                PatternRewriter &rewriter) const override {
    auto bufferValue = rewriter.createOrFold<BufferViewBufferOp>(
        op.getLoc(), IREE::HAL::BufferType::get(rewriter.getContext()),
        op.buffer_view());
    auto allocatorValue = rewriter.createOrFold<BufferAllocatorOp>(
        op.getLoc(), AllocatorType::get(rewriter.getContext()), bufferValue);
    int rank = op.indices().size();
    SmallVector<Type, 4> dimTypes(rank, rewriter.getIndexType());
    auto dimsOp = rewriter.create<BufferViewDimsOp>(op.getLoc(), dimTypes,
                                                    op.buffer_view());
    auto elementTypeValue = rewriter.createOrFold<BufferViewElementTypeOp>(
        op.getLoc(), rewriter.getI32Type(), op.buffer_view());
    rewriter.replaceOpWithNewOp<AllocatorComputeRangeOp>(
        op, allocatorValue, dimsOp.result(), elementTypeValue, op.indices(),
        op.lengths());
    return success();
  }
};

}  // namespace

void BufferViewComputeRangeOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ExpandBufferViewComputeRangeOp>(context);
}

namespace {

/// Expands a hal.buffer_view.dims op into individual ops for each dimension.
struct ExpandBufferViewDimsOp : public OpRewritePattern<BufferViewDimsOp> {
  using OpRewritePattern<BufferViewDimsOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(BufferViewDimsOp op,
                                PatternRewriter &rewriter) const override {
    SmallVector<Value, 4> newDimValues;
    for (unsigned i = 0; i < op.getNumResults(); ++i) {
      newDimValues.push_back(rewriter.createOrFold<BufferViewDimOp>(
          op.getLoc(), rewriter.getIndexType(), op.buffer_view(),
          rewriter.getIndexAttr(i)));
    }
    rewriter.replaceOp(op, {newDimValues});
    return success();
  }
};

}  // namespace

void BufferViewDimsOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ExpandBufferViewDimsOp>(context);
}

//===----------------------------------------------------------------------===//
// hal.command_buffer.*
//===----------------------------------------------------------------------===//

namespace {

/// Skips a hal.command_buffer.device accessor when the device was created in
/// the same scope.
struct SkipCommandBufferDeviceOp
    : public OpRewritePattern<CommandBufferDeviceOp> {
  using OpRewritePattern<CommandBufferDeviceOp>::OpRewritePattern;

  LogicalResult matchAndRewrite(CommandBufferDeviceOp op,
                                PatternRewriter &rewriter) const override {
    if (auto createOp = dyn_cast_or_null<CommandBufferCreateOp>(
            op.command_buffer().getDefiningOp())) {
      rewriter.replaceOp(op, createOp.device());
      return success();
    }
    return failure();
  }
};

}  // namespace

void CommandBufferDeviceOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<SkipCommandBufferDeviceOp>(context);
}

namespace {

/// Folds hal.buffer.subspans into buffer fill offsets.
struct FoldCommandBufferFillBufferSubspans
    : public OpRewritePattern<CommandBufferFillBufferOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(CommandBufferFillBufferOp op,
                                PatternRewriter &rewriter) const override {
    auto ip = rewriter.saveInsertionPoint();
    rewriter.setInsertionPoint(op);
    bool needsUpdate = false;
    auto newTargetBuffer = op.target_buffer();
    auto newTargetOffset = op.target_offset();
    if (auto subspanOp = dyn_cast_or_null<BufferSubspanOp>(
            op.target_buffer().getDefiningOp())) {
      newTargetBuffer = subspanOp.source_buffer();
      newTargetOffset = rewriter.createOrFold<mlir::AddIOp>(
          subspanOp.getLoc(), subspanOp.source_offset(), op.target_offset());
      needsUpdate = true;
    }
    rewriter.restoreInsertionPoint(ip);
    if (!needsUpdate) return failure();
    rewriter.updateRootInPlace(op, [&]() {
      op.target_bufferMutable().assign(newTargetBuffer);
      op.target_offsetMutable().assign(newTargetOffset);
    });
    return success();
  }
};

}  // namespace

void CommandBufferFillBufferOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<FoldCommandBufferFillBufferSubspans>(context);
}

namespace {

/// Folds hal.buffer.subspans into buffer copy offsets.
struct FoldCommandBufferCopyBufferSubspans
    : public OpRewritePattern<CommandBufferCopyBufferOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(CommandBufferCopyBufferOp op,
                                PatternRewriter &rewriter) const override {
    auto ip = rewriter.saveInsertionPoint();
    rewriter.setInsertionPoint(op);
    bool needsUpdate = false;
    auto newSourceBuffer = op.source_buffer();
    auto newSourceOffset = op.source_offset();
    if (auto subspanOp = dyn_cast_or_null<BufferSubspanOp>(
            op.source_buffer().getDefiningOp())) {
      newSourceBuffer = subspanOp.source_buffer();
      newSourceOffset = rewriter.createOrFold<mlir::AddIOp>(
          subspanOp.getLoc(), subspanOp.source_offset(), op.source_offset());
      needsUpdate = true;
    }
    auto newTargetBuffer = op.target_buffer();
    auto newTargetOffset = op.target_offset();
    if (auto subspanOp = dyn_cast_or_null<BufferSubspanOp>(
            op.target_buffer().getDefiningOp())) {
      newTargetBuffer = subspanOp.source_buffer();
      newTargetOffset = rewriter.createOrFold<mlir::AddIOp>(
          subspanOp.getLoc(), subspanOp.source_offset(), op.target_offset());
      needsUpdate = true;
    }
    rewriter.restoreInsertionPoint(ip);
    if (!needsUpdate) return failure();
    rewriter.updateRootInPlace(op, [&]() {
      op.source_bufferMutable().assign(newSourceBuffer);
      op.source_offsetMutable().assign(newSourceOffset);
      op.target_bufferMutable().assign(newTargetBuffer);
      op.target_offsetMutable().assign(newTargetOffset);
    });
    return success();
  }
};

}  // namespace

void CommandBufferCopyBufferOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<FoldCommandBufferCopyBufferSubspans>(context);
}

namespace {

/// Folds hal.buffer.subspans into push descriptor bindings.
/// The binding range is always equal to or a subset of the subspan.
struct FoldCommandBufferPushDescriptorSetBufferSubspan
    : public OpRewritePattern<CommandBufferPushDescriptorSetOp> {
  using OpRewritePattern::OpRewritePattern;

  LogicalResult matchAndRewrite(CommandBufferPushDescriptorSetOp op,
                                PatternRewriter &rewriter) const override {
    auto ip = rewriter.saveInsertionPoint();
    rewriter.setInsertionPoint(op);
    bool needsUpdate = false;
    auto bindingBuffers = llvm::to_vector<4>(op.binding_buffers());
    auto bindingOffsets = llvm::to_vector<4>(op.binding_offsets());
    for (size_t i = 0; i < bindingBuffers.size(); ++i) {
      auto *definingOp = bindingBuffers[i].getDefiningOp();
      if (!definingOp) continue;
      if (auto subspanOp = dyn_cast<BufferSubspanOp>(definingOp)) {
        needsUpdate = true;
        bindingBuffers[i] = subspanOp.source_buffer();
        bindingOffsets[i] = rewriter.createOrFold<mlir::AddIOp>(
            subspanOp.getLoc(), subspanOp.source_offset(), bindingOffsets[i]);
      }
    }
    rewriter.restoreInsertionPoint(ip);
    if (!needsUpdate) return failure();
    rewriter.updateRootInPlace(op, [&]() {
      auto mutableBindingBuffers = op.binding_buffersMutable();
      mutableBindingBuffers.clear();
      mutableBindingBuffers.append(bindingBuffers);
      auto mutableBindingOffsets = op.binding_offsetsMutable();
      mutableBindingOffsets.clear();
      mutableBindingOffsets.append(bindingOffsets);
    });
    return success();
  }
};

}  // namespace

void CommandBufferPushDescriptorSetOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<FoldCommandBufferPushDescriptorSetBufferSubspan>(context);
}

//===----------------------------------------------------------------------===//
// hal.constant_pool.*
//===----------------------------------------------------------------------===//

namespace {

// Resolves hal.constant.buffer ops to their runtime hal.variable buffer.
struct ResolveConstantPoolLoadToRuntimeBuffer
    : public OpRewritePattern<ConstantPoolLoadOp> {
  using OpRewritePattern<ConstantPoolLoadOp>::OpRewritePattern;
  LogicalResult matchAndRewrite(ConstantPoolLoadOp op,
                                PatternRewriter &rewriter) const override {
    auto *constOp = SymbolTable::lookupNearestSymbolFrom(op, op.constant());
    SymbolRefAttr runtimeBufferSymRef;
    ByteRangeAttr runtimeBufferRange;
    if (auto spanOp = dyn_cast<ConstantPoolSpanOp>(constOp)) {
      runtimeBufferSymRef = spanOp.runtime_bufferAttr();
      runtimeBufferRange = spanOp.runtime_rangeAttr();
    } else if (auto splatOp = dyn_cast<ConstantPoolSplatOp>(constOp)) {
      runtimeBufferSymRef = splatOp.runtime_bufferAttr();
      runtimeBufferRange = splatOp.runtime_rangeAttr();
    }
    if (!runtimeBufferSymRef || !runtimeBufferRange) return failure();
    rewriter.replaceOpWithNewOp<IREE::HAL::ConstantSubspanOp>(
        op, op.getType(), runtimeBufferSymRef, runtimeBufferRange);
    return success();
  }
};

}  // namespace

void ConstantPoolLoadOp::getCanonicalizationPatterns(
    OwningRewritePatternList &results, MLIRContext *context) {
  results.insert<ResolveConstantPoolLoadToRuntimeBuffer>(context);
}

//===----------------------------------------------------------------------===//
// hal.device.switch
//===----------------------------------------------------------------------===//

// TODO(benvanik): fold conditions with the same IR tree.
// TODO(benvanik): remove duplicate conditions.
// TODO(benvanik): fold condition expressions (any(always, ...) -> always, etc).
// TODO(benvanik): completely replace switches with just one always block.
// TODO(benvanik): remove conditions with no side-effects.

//===----------------------------------------------------------------------===//
// hal.device.match.id
//===----------------------------------------------------------------------===//

// TODO(benvanik): fold matches that are known true based on device config.

}  // namespace HAL
}  // namespace IREE
}  // namespace iree_compiler
}  // namespace mlir
