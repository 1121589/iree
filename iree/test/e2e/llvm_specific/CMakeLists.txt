################################################################################
# Autogenerated by build_tools/bazel_to_cmake/bazel_to_cmake.py from           #
# iree/test/e2e/llvm_specific/BUILD                                            #
#                                                                              #
# Use iree_cmake_extra_content from iree/build_defs.oss.bzl to add arbitrary   #
# CMake-only content.                                                          #
#                                                                              #
# To disable autogeneration for this file entirely, delete this header.        #
################################################################################

iree_add_all_subdirs()

iree_check_single_backend_test_suite(
  NAME
    check_llvm-aot-conv_img2col
  SRCS
    "conv.mlir"
  TARGET_BACKEND
    "dylib-llvm-aot"
  DRIVER
    "dylib"
  COMPILER_FLAGS
    "-iree-codegen-linalg-to-llvm-conv-img2col-conversion=true"
)

### BAZEL_TO_CMAKE_PRESERVES_ALL_CONTENT_BELOW_THIS_LINE ###
