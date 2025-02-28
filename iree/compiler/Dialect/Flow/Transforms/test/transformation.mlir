// RUN: iree-opt -split-input-file -iree-input-transformation-pipeline -iree-flow-transformation-pipeline %s | IreeFileCheck %s

// CHECK-LABEL: @empty
func @empty() {
  // CHECK-NEXT: return
  return
}

// -----

func @stdElementwiseOps(%arg0 : tensor<4xf32>) -> tensor<4xf32> {
  %0 = addf %arg0, %arg0 : tensor<4xf32>
  %1 = subf %0, %arg0 : tensor<4xf32>
  %2 = mulf %1, %arg0 : tensor<4xf32>
  return %2 : tensor<4xf32>
}

// CHECK-LABEL: flow.executable @stdElementwiseOps_ex_dispatch_0 attributes {sym_visibility = "private"} {
// CHECK-NEXT:   flow.dispatch.entry @stdElementwiseOps_ex_dispatch_0
// CHECK-NEXT:   module {
// CHECK-NEXT:     func @stdElementwiseOps_ex_dispatch_0(%arg0: tensor<4xf32>) -> tensor<4xf32> {
// CHECK-NEXT:       %0 = addf %arg0, %arg0 : tensor<4xf32>
// CHECK-NEXT:       %1 = subf %0, %arg0 : tensor<4xf32>
// CHECK-NEXT:       %2 = mulf %1, %arg0 : tensor<4xf32>
// CHECK-NEXT:       return %2 : tensor<4xf32>
// CHECK-NEXT:     }
// CHECK-NEXT:   }
// CHECK-NEXT: }
// CHECK-NEXT: func @stdElementwiseOps(%arg0: tensor<4xf32>) -> tensor<4xf32> {
// CHECK-NEXT:   %0 = flow.ex.stream.fragment(%arg0) : (tensor<4xf32>) -> tensor<4xf32> =
// CHECK-NEXT:       (%arg1: tensor<4xf32>) -> tensor<4xf32> {
// CHECK-NEXT:     %[[WORKLOAD:.+]] = constant 4 : index
// CHECK-NEXT:     %1 = flow.dispatch @stdElementwiseOps_ex_dispatch_0::@stdElementwiseOps_ex_dispatch_0[%[[WORKLOAD]]](%arg1) : (tensor<4xf32>) -> tensor<4xf32>
// CHECK-NEXT:     flow.return %1 : tensor<4xf32>
// CHECK-NEXT:   }
// CHECK-NEXT:   return %0 : tensor<4xf32>
// CHECK-NEXT: }

// -----

func @hloElementwiseOps(%arg0 : tensor<4xf32>) -> tensor<4xf32> {
  %0 = mhlo.add %arg0, %arg0 : tensor<4xf32>
  %1 = mhlo.subtract %0, %arg0 : tensor<4xf32>
  %2 = mhlo.multiply %1, %arg0 : tensor<4xf32>
  return %2 : tensor<4xf32>
}

// CHECK-LABEL: flow.executable @hloElementwiseOps_ex_dispatch_0 attributes {sym_visibility = "private"} {
// CHECK-NEXT:   flow.dispatch.entry @hloElementwiseOps_ex_dispatch_0
// CHECK-NEXT:   module {
// CHECK-NEXT:     func @hloElementwiseOps_ex_dispatch_0(%arg0: tensor<4xf32>) -> tensor<4xf32> {
// CHECK-NEXT:       %0 = mhlo.add %arg0, %arg0 : tensor<4xf32>
// CHECK-NEXT:       %1 = mhlo.subtract %0, %arg0 : tensor<4xf32>
// CHECK-NEXT:       %2 = mhlo.multiply %1, %arg0 : tensor<4xf32>
// CHECK-NEXT:       return %2 : tensor<4xf32>
// CHECK-NEXT:     }
// CHECK-NEXT:   }
// CHECK-NEXT: }
// CHECK-NEXT: func @hloElementwiseOps(%arg0: tensor<4xf32>) -> tensor<4xf32> {
// CHECK-NEXT:   %0 = flow.ex.stream.fragment(%arg0) : (tensor<4xf32>) -> tensor<4xf32> =
// CHECK-NEXT:       (%arg1: tensor<4xf32>) -> tensor<4xf32> {
// CHECK-NEXT:     %[[WORKLOAD:.+]] = constant 4 : index
// CHECK-NEXT:     %1 = flow.dispatch @hloElementwiseOps_ex_dispatch_0::@hloElementwiseOps_ex_dispatch_0[%[[WORKLOAD]]](%arg1) : (tensor<4xf32>) -> tensor<4xf32>
// CHECK-NEXT:     flow.return %1 : tensor<4xf32>
// CHECK-NEXT:   }
// CHECK-NEXT:   return %0 : tensor<4xf32>
// CHECK-NEXT: }

// -----

func @interleavedDot(%arg0 : tensor<4x4xf32>) -> tensor<4x4xf32> {
  %0 = mhlo.add %arg0, %arg0 : tensor<4x4xf32>
  %1 = "mhlo.dot"(%0, %arg0) : (tensor<4x4xf32>, tensor<4x4xf32>) -> tensor<4x4xf32>
  %2 = mhlo.multiply %1, %arg0 : tensor<4x4xf32>
  return %2 : tensor<4x4xf32>
}

// CHECK-LABEL: flow.executable @interleavedDot_ex_dispatch_0 attributes {sym_visibility = "private"} {
// CHECK-NEXT:   flow.dispatch.entry @interleavedDot_ex_dispatch_0
// CHECK-NEXT:   module {
// CHECK-NEXT:     func @interleavedDot_ex_dispatch_0(%arg0: tensor<4x4xf32>) -> tensor<4x4xf32> {
// CHECK-NEXT:       %0 = mhlo.add %arg0, %arg0 : tensor<4x4xf32>
// CHECK-NEXT:       return %0 : tensor<4x4xf32>
// CHECK-NEXT:     }
// CHECK-NEXT:   }
// CHECK-NEXT: }
// CHECK-NEXT: flow.executable @interleavedDot_ex_dispatch_1 attributes {sym_visibility = "private"} {
// CHECK-NEXT:   flow.dispatch.entry @interleavedDot_ex_dispatch_1
// CHECK-NEXT:   module {
// CHECK-NEXT:     func @interleavedDot_ex_dispatch_1(%arg0: tensor<4x4xf32>, %arg1: tensor<4x4xf32>) -> tensor<4x4xf32> {
// CHECK-NEXT:       %0 = "mhlo.dot"(%arg0, %arg1) : (tensor<4x4xf32>, tensor<4x4xf32>) -> tensor<4x4xf32>
// CHECK-NEXT:       return %0 : tensor<4x4xf32>
// CHECK-NEXT:     }
// CHECK-NEXT:   }
// CHECK-NEXT: }
// CHECK-NEXT: flow.executable @interleavedDot_ex_dispatch_2 attributes {sym_visibility = "private"} {
// CHECK-NEXT:   flow.dispatch.entry @interleavedDot_ex_dispatch_2
// CHECK-NEXT:   module {
// CHECK-NEXT:     func @interleavedDot_ex_dispatch_2(%arg0: tensor<4x4xf32>, %arg1: tensor<4x4xf32>) -> tensor<4x4xf32> {
// CHECK-NEXT:       %0 = mhlo.multiply %arg0, %arg1 : tensor<4x4xf32>
// CHECK-NEXT:       return %0 : tensor<4x4xf32>
// CHECK-NEXT:     }
// CHECK-NEXT:   }
// CHECK-NEXT: }
// CHECK-NEXT: func @interleavedDot(%arg0: tensor<4x4xf32>) -> tensor<4x4xf32> {
// CHECK-NEXT:   %0 = flow.ex.stream.fragment(%arg0) : (tensor<4x4xf32>) -> tensor<4x4xf32> =
// CHECK-NEXT:        (%arg1: tensor<4x4xf32>) -> tensor<4x4xf32> {
// CHECK-NEXT:     %[[WORKLOAD:.+]] = constant 16 : index
// CHECK-NEXT:     %1 = flow.dispatch @interleavedDot_ex_dispatch_0::@interleavedDot_ex_dispatch_0[%[[WORKLOAD]]](%arg1) : (tensor<4x4xf32>) -> tensor<4x4xf32>
// CHECK-NEXT:     %2 = flow.dispatch @interleavedDot_ex_dispatch_1::@interleavedDot_ex_dispatch_1[%[[WORKLOAD]]](%1, %arg1) : (tensor<4x4xf32>, tensor<4x4xf32>) -> tensor<4x4xf32>
// CHECK-NEXT:     %3 = flow.dispatch @interleavedDot_ex_dispatch_2::@interleavedDot_ex_dispatch_2[%[[WORKLOAD]]](%2, %arg1) : (tensor<4x4xf32>, tensor<4x4xf32>) -> tensor<4x4xf32>
// CHECK-NEXT:     flow.return %3 : tensor<4x4xf32>
// CHECK-NEXT:   }
// CHECK-NEXT:   return %0 : tensor<4x4xf32>
// CHECK-NEXT: }

// -----

func @reduction(%arg0 : tensor<4x8xf32>) -> tensor<4xf32> {
  %0 = constant dense<0.0> : tensor<f32>
  %1 = "mhlo.reduce"(%arg0, %0) ( {
  ^bb0(%arg1 : tensor<f32>, %arg2 : tensor<f32>):
    %2 = mhlo.add %arg1, %arg2 : tensor<f32>
    "mhlo.return"(%2) : (tensor<f32>) -> ()
  }) {dimensions = dense<[1]> : tensor<1xi64>} : (tensor<4x8xf32>, tensor<f32>) -> tensor<4xf32>
  return %1 : tensor<4xf32>
}

// CHECK-LABEL: flow.executable @reduction_ex_dispatch_0 attributes {sym_visibility = "private"} {
//  CHECK-NEXT:   flow.dispatch.entry @reduction_ex_dispatch_0
//  CHECK-NEXT:   module {
//  CHECK-NEXT:     func @reduction_ex_dispatch_0(%arg0: tensor<4x8xf32>) -> tensor<4xf32> {
//  CHECK-NEXT:       %cst = constant dense<0.000000e+00> : tensor<f32>
//  CHECK-NEXT:       %0 = "mhlo.reduce"(%arg0, %cst) ( {
//  CHECK-NEXT:       ^bb0(%arg1: tensor<f32>, %arg2: tensor<f32>): // no predecessors
//  CHECK-NEXT:         %1 = mhlo.add %arg1, %arg2 : tensor<f32>
//  CHECK-NEXT:         "mhlo.return"(%1) : (tensor<f32>) -> ()
//  CHECK-NEXT:       }) {dimensions = dense<1> : tensor<1xi64>} : (tensor<4x8xf32>, tensor<f32>) -> tensor<4xf32>
//  CHECK-NEXT:       return %0 : tensor<4xf32>
//  CHECK-NEXT:     }
//  CHECK-NEXT:   }
//  CHECK-NEXT: }
//  CHECK-NEXT: func @reduction(%arg0: tensor<4x8xf32>) -> tensor<4xf32> {
//  CHECK-NEXT:   %0 = flow.ex.stream.fragment(%arg0) : (tensor<4x8xf32>) -> tensor<4xf32> =
//  CHECK-NEXT:       (%arg1: tensor<4x8xf32>) -> tensor<4xf32> {
//  CHECK-NEXT:     %[[WORKLOAD:.+]] = constant 4 : index
//  CHECK-NEXT:     %1 = flow.dispatch @reduction_ex_dispatch_0::@reduction_ex_dispatch_0[%[[WORKLOAD]]](%arg1) : (tensor<4x8xf32>) -> tensor<4xf32>
//  CHECK-NEXT:     flow.return %1 : tensor<4xf32>
//  CHECK-NEXT:   }
//  CHECK-NEXT:   return %0 : tensor<4xf32>
//  CHECK-NEXT: }

// -----

func @dynamicUpdateSlice(%operand : tensor<2x4xi32>, %update : tensor<1x1xi32>, %indices_0 : tensor<i64>, %indices_1 : tensor<i64>) -> tensor<2x4xi32> {
  %0 = "mhlo.dynamic-update-slice"(%operand, %update, %indices_0, %indices_1) : (tensor<2x4xi32>, tensor<1x1xi32>, tensor<i64>, tensor<i64>) -> tensor<2x4xi32>
  %1 = mhlo.add %operand, %0 : tensor<2x4xi32>
  return %1 : tensor<2x4xi32>
}

// CHECK-LABEL: flow.executable @dynamicUpdateSlice_ex_dispatch_0 attributes {sym_visibility = "private"} {
// CHECK-NEXT: flow.dispatch.entry @dynamicUpdateSlice_ex_dispatch_0
// CHECK-NEXT:   module {
// CHECK-NEXT:     func @dynamicUpdateSlice_ex_dispatch_0(%arg0: tensor<2x4xi32>, %arg1: tensor<2x4xi32>) -> tensor<2x4xi32> {
// CHECK-NEXT:       %0 = mhlo.add %arg0, %arg1 : tensor<2x4xi32>
// CHECK-NEXT:       return %0 : tensor<2x4xi32>
// CHECK-NEXT:     }
// CHECK-NEXT:   }
// CHECK-NEXT: }
// CHECK-NEXT: func @dynamicUpdateSlice(%arg0: tensor<2x4xi32>, %arg1: tensor<1x1xi32>, %arg2: tensor<i32>, %arg3: tensor<i32>) -> tensor<2x4xi32> {
//  CHECK-DAG:   %[[ARG2_LOAD:.+]] = flow.tensor.load %arg2 : tensor<i32>
//  CHECK-DAG:   %[[ARG2_INDEX:.+]] = index_cast %[[ARG2_LOAD]] : i32 to index
//  CHECK-DAG:   %[[ARG3_LOAD:.+]] = flow.tensor.load %arg3 : tensor<i32>
//  CHECK-DAG:   %[[ARG3_INDEX:.+]] = index_cast %[[ARG3_LOAD]] : i32 to index
// CHECK-NEXT:   %[[RET:.+]] = flow.ex.stream.fragment(%arg0, %[[ARG2_INDEX]], %[[ARG3_INDEX]], %arg1) : (tensor<2x4xi32>, index, index, tensor<1x1xi32>) -> tensor<2x4xi32> =
// CHECK-NEXT:        (%arg4: tensor<2x4xi32>, %arg5: index, %arg6: index, %arg7: tensor<1x1xi32>) -> tensor<2x4xi32> {
// CHECK-NEXT:     %[[WORKLOAD:.+]] = constant 8 : index
// CHECK-NEXT:     %[[ARG4_CLONE:.+]] = flow.tensor.clone %arg4 : tensor<2x4xi32>
// CHECK-NEXT:     %[[T0:.+]] = flow.tensor.update %arg7, %arg4[%arg5, %arg6] : tensor<1x1xi32> -> tensor<2x4xi32>
// CHECK-NEXT:     %[[T1:.+]] = flow.dispatch @dynamicUpdateSlice_ex_dispatch_0::@dynamicUpdateSlice_ex_dispatch_0[%[[WORKLOAD]]](%[[ARG4_CLONE]], %[[T0]]) : (tensor<2x4xi32>, tensor<2x4xi32>) -> tensor<2x4xi32>
// CHECK-NEXT:     flow.return %[[T1]] : tensor<2x4xi32>
// CHECK-NEXT:   }
// CHECK-NEXT:   return %[[RET]] : tensor<2x4xi32>
// CHECK-NEXT: }
