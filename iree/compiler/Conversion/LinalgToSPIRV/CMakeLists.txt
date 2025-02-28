################################################################################
# Autogenerated by build_tools/bazel_to_cmake/bazel_to_cmake.py from           #
# iree/compiler/Conversion/LinalgToSPIRV/BUILD                                 #
#                                                                              #
# Use iree_cmake_extra_content from iree/build_defs.oss.bzl to add arbitrary   #
# CMake-only content.                                                          #
#                                                                              #
# To disable autogeneration for this file entirely, delete this header.        #
################################################################################

iree_add_all_subdirs()

iree_cc_library(
  NAME
    CodeGenOptionUtils
  HDRS
    "CodeGenOptionUtils.h"
  SRCS
    "CodeGenOptionUtils.cpp"
  DEPS
    LLVMSupport
  PUBLIC
)

iree_cc_library(
  NAME
    LinalgToSPIRV
  HDRS
    "KernelDispatchUtils.h"
    "MemorySpace.h"
    "Passes.h"
    "Utils.h"
  SRCS
    "ConcretizeTileAmongWorkgroupsPass.cpp"
    "ConvertToGPUPass.cpp"
    "ConvertToSPIRVPass.cpp"
    "FoldGPUProcessorIDUses.cpp"
    "KernelDispatchUtils.cpp"
    "Passes.cpp"
    "TileAndVectorizeInOneWorkgroupPass.cpp"
    "Utils.cpp"
    "VectorToCooperativeMatrixPass.cpp"
    "VectorToGPUPass.cpp"
    "VectorizeMemrefLoadStorePass.cpp"
  DEPS
    ::CodeGenOptionUtils
    LLVMSupport
    MLIRAffine
    MLIRAffineToStandard
    MLIRAnalysis
    MLIRGPU
    MLIRGPUToSPIRV
    MLIRIR
    MLIRLinalg
    MLIRLinalgTransforms
    MLIRMemRef
    MLIRMemRefTransforms
    MLIRPass
    MLIRSCF
    MLIRSCFToGPU
    MLIRSCFToSPIRV
    MLIRSPIRV
    MLIRSPIRVConversion
    MLIRSPIRVTransforms
    MLIRSideEffectInterfaces
    MLIRStandard
    MLIRStandardToSPIRV
    MLIRSupport
    MLIRTosa
    MLIRTosaToStandard
    MLIRTransforms
    MLIRVector
    MLIRVectorToSPIRV
    iree::compiler::Conversion::CodegenUtils
    iree::compiler::Conversion::Common
    iree::compiler::Conversion::HLOToHLO
    iree::compiler::Conversion::HLOToLinalg
    iree::compiler::Conversion::LinalgToVector
    iree::compiler::Dialect::Flow::IR
    iree::compiler::Dialect::HAL::IR
    iree::compiler::Dialect::HAL::IR::HALDialect
    iree::compiler::Dialect::IREE::IR
    iree::compiler::Dialect::Shape::IR
    iree::compiler::Dialect::Shape::Transforms
    tensorflow::mlir_hlo
  PUBLIC
)

### BAZEL_TO_CMAKE_PRESERVES_ALL_CONTENT_BELOW_THIS_LINE ###
