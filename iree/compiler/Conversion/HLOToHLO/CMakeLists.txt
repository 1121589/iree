################################################################################
# Autogenerated by build_tools/bazel_to_cmake/bazel_to_cmake.py from           #
# iree/compiler/Conversion/HLOToHLO/BUILD                                      #
#                                                                              #
# Use iree_cmake_extra_content from iree/build_defs.oss.bzl to add arbitrary   #
# CMake-only content.                                                          #
#                                                                              #
# To disable autogeneration for this file entirely, delete this header.        #
################################################################################

iree_add_all_subdirs()

iree_cc_library(
  NAME
    HLOToHLO
  HDRS
    "Passes.h"
  SRCS
    "Convert1x1ConvToDot.cpp"
    "DecomposeHLOClamp.cpp"
    "DemoteF32ToF16.cpp"
  DEPS
    LLVMSupport
    MLIRIR
    MLIRPass
    MLIRSupport
    MLIRTransformUtils
    iree::compiler::Dialect::Flow::IR
    iree::compiler::Dialect::IREE::IR
    tensorflow::mlir_hlo
  PUBLIC
)

### BAZEL_TO_CMAKE_PRESERVES_ALL_CONTENT_BELOW_THIS_LINE ###
