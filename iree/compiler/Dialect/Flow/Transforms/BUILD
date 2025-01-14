# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      https://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

load("//build_tools/bazel:tblgen.bzl", "gentbl")
load("//iree:build_defs.oss.bzl", "iree_cmake_extra_content")

package(
    default_visibility = ["//visibility:public"],
    features = ["layering_check"],
    licenses = ["notice"],  # Apache 2.0
)

gentbl(
    name = "Passes_inc_gen",
    tbl_outs = [
        (
            "-gen-pass-decls",
            "Passes.h.inc",
        ),
    ],
    tblgen = "@llvm-project//mlir:mlir-tblgen",
    td_file = "Passes.td",
    td_srcs = [
        "@llvm-project//mlir:PassBaseTdFiles",
    ],
)

cc_library(
    name = "Transforms",
    srcs = [
        "ConvertToFlowTensorOps.cpp",
        "DeduplicateExecutables.cpp",
        "DestructiveUpdateUtils.cpp",
        "DispatchConfig.cpp",
        "DispatchLinalgOnTensors.cpp",
        "DispatchabilityAnalysis.cpp",
        "ExpandVariableDynamicDims.cpp",
        "ExportBenchmarkFuncs.cpp",
        "FoldCompatibleDispatchRegions.cpp",
        "FormStreams.cpp",
        "HLOToHLOPreprocessing.cpp",
        "HoistUnstreamableOps.cpp",
        "IdentifyDispatchRegions2.cpp",
        "InjectDispatchTracing.cpp",
        "LegalizeInputTypes.cpp",
        "OutlineDispatchRegions.cpp",
        "OutlineDispatchRegions2.cpp",
        "OutlineLargeConstants.cpp",
        "PassDetail.h",
        "Passes.cpp",
        "PrePostPartitioningConversion.cpp",
        "StripAndSplatConstantVariables.cpp",
    ],
    hdrs = [
        "DestructiveUpdateUtils.h",
        "DispatchConfig.h",
        "Passes.h",
        "Passes.h.inc",
    ],
    deps = [
        ":Passes_inc_gen",
        "//iree/compiler/Conversion/HLOToHLO",
        "//iree/compiler/Conversion/HLOToLinalg:HLOToLinalgOnTensors",
        "//iree/compiler/Conversion/LinalgToLinalg",
        "//iree/compiler/Dialect/Flow/Analysis",
        "//iree/compiler/Dialect/Flow/Conversion",
        "//iree/compiler/Dialect/Flow/Conversion/HLOToFlow",
        "//iree/compiler/Dialect/Flow/Conversion/StandardToFlow",
        "//iree/compiler/Dialect/Flow/IR",
        "//iree/compiler/Dialect/Flow/Utils",
        "//iree/compiler/Dialect/HAL/IR",
        "//iree/compiler/Dialect/IREE/IR",
        "//iree/compiler/Dialect/Shape/Conversion",
        "//iree/compiler/Dialect/Shape/IR",
        "//iree/compiler/Dialect/Shape/Transforms",
        "//iree/compiler/Dialect/Shape/Utils:TypeConversion",
        "//iree/compiler/Utils",
        "@llvm-project//llvm:Support",
        "@llvm-project//mlir:IR",
        "@llvm-project//mlir:LinalgOps",
        "@llvm-project//mlir:LinalgTransforms",
        "@llvm-project//mlir:MemRefDialect",
        "@llvm-project//mlir:Pass",
        "@llvm-project//mlir:SCFDialect",
        "@llvm-project//mlir:SCFToStandard",
        "@llvm-project//mlir:Shape",
        "@llvm-project//mlir:ShapeTransforms",
        "@llvm-project//mlir:StandardOps",
        "@llvm-project//mlir:Support",
        "@llvm-project//mlir:TensorDialect",
        "@llvm-project//mlir:TosaToLinalg",
        "@llvm-project//mlir:TosaToSCF",
        "@llvm-project//mlir:TosaToStandard",
        "@llvm-project//mlir:TransformUtils",
        "@llvm-project//mlir:Transforms",
        "@mlir-hlo//:chlo_legalize_to_hlo",
        "@mlir-hlo//:hlo",
        "@mlir-hlo//:legalize_control_flow",
        "@mlir-hlo//:legalize_gather_to_torch_index_select",
        "@mlir-hlo//:materialize_broadcasts",
        "@mlir-hlo//:mhlo_to_mhlo_lowering_patterns",
        "@mlir-hlo//:unfuse_batch_norm",
    ],
)

# TODO(#4919): For an unknown reason, GCC's devirtualization optimization wreaks
# havoc on this file. Needs to be further root caused. Seems to affect both 9.x
# and 10.x.
iree_cmake_extra_content(
    content = """
set_property(SOURCE
  DispatchLinalgOnTensors.cpp
  PROPERTY COMPILE_FLAGS $<$<CXX_COMPILER_ID:GNU>:-fno-devirtualize>)
""",
    inline = True,
)
