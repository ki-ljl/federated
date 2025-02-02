/* Copyright 2021, The TensorFlow Federated Authors.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

     http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License
==============================================================================*/

#ifndef THIRD_PARTY_TENSORFLOW_FEDERATED_CC_CORE_IMPL_EXECUTORS_VALUE_TEST_UTILS_H_
#define THIRD_PARTY_TENSORFLOW_FEDERATED_CC_CORE_IMPL_EXECUTORS_VALUE_TEST_UTILS_H_

#include <cstdint>
#include <functional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "google/protobuf/any.pb.h"
#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"
#include "absl/types/span.h"
#include "tensorflow/cc/client/client_session.h"
#include "tensorflow/cc/framework/ops.h"
#include "tensorflow/cc/framework/scope.h"
#include "tensorflow/cc/ops/const_op.h"
#include "tensorflow/cc/ops/dataset_ops_internal.h"
#include "tensorflow/core/framework/tensor.h"
#include "tensorflow/core/framework/tensor.pb.h"
#include "tensorflow/core/framework/tensor_shape.h"
#include "tensorflow/core/framework/tensor_types.h"
#include "tensorflow/core/framework/types.pb.h"
#include "tensorflow/core/platform/tstring.h"
#include "tensorflow_federated/cc/core/impl/executors/dataset_conversions.h"
#include "tensorflow_federated/cc/core/impl/executors/executor.h"
#include "tensorflow_federated/cc/core/impl/executors/protobuf_matchers.h"
#include "tensorflow_federated/proto/v0/computation.pb.h"
#include "tensorflow_federated/proto/v0/executor.pb.h"

namespace tensorflow_federated {
namespace testing {

inline v0::Value IntrinsicV(absl::string_view uri) {
  v0::Value value_proto;
  // Construct an explicit string from this string-view; this silent conversion
  // is not present in OSS.
  *value_proto.mutable_computation()->mutable_intrinsic()->mutable_uri() =
      std::string(uri);
  return value_proto;
}

// NOTE: Returns a value whose federated type `.member` field is unset.
inline v0::Value ServerV(v0::Value server_val) {
  v0::Value value_proto;
  v0::FederatedType* type_proto =
      value_proto.mutable_federated()->mutable_type();
  type_proto->set_all_equal(true);
  *type_proto->mutable_placement()->mutable_value()->mutable_uri() = "server";
  *value_proto.mutable_federated()->add_value() = server_val;
  return value_proto;
}

// NOTE: Returns a value whose federated type `.member` field is unset.
inline v0::Value ClientsV(const absl::Span<const v0::Value> client_values,
                          bool all_equal = false) {
  v0::Value value_proto;
  v0::FederatedType* type_proto =
      value_proto.mutable_federated()->mutable_type();
  type_proto->set_all_equal(all_equal);
  *type_proto->mutable_placement()->mutable_value()->mutable_uri() = "clients";
  auto values_pb = value_proto.mutable_federated()->mutable_value();
  values_pb->Reserve(client_values.size());
  for (const auto& client_value : client_values) {
    *values_pb->Add() = client_value;
  }
  return value_proto;
}

template <typename... Ts>
v0::Value TensorV(Ts... tensor_constructor_args) {
  tensorflow::Tensor tensor(tensor_constructor_args...);
  tensorflow::TensorProto tensor_proto;
  if (tensor.dtype() == tensorflow::DT_STRING) {
    tensor.AsProtoField(&tensor_proto);
  } else {
    tensor.AsProtoTensorContent(&tensor_proto);
  }
  v0::Value value_proto;
  value_proto.mutable_tensor()->PackFrom(tensor_proto);
  return value_proto;
}

inline v0::Value TensorVFromIntList(absl::Span<const int32_t> elements) {
  size_t num_elements = elements.size();
  tensorflow::TensorShape shape({static_cast<int64_t>(num_elements)});
  tensorflow::Tensor tensor(tensorflow::DT_INT32, shape);
  auto flat = tensor.flat<int32_t>();
  for (size_t i = 0; i < num_elements; i++) {
    flat(i) = elements[i];
  }
  return TensorV(tensor);
}

inline v0::Value StructV(const absl::Span<const v0::Value> elements) {
  v0::Value value_proto;
  auto struct_proto = value_proto.mutable_struct_();
  for (const auto& element : elements) {
    *struct_proto->add_element()->mutable_value() = element;
  }
  return value_proto;
}

// Returns the string representation of a TensorFlow GraphDef representing a
// dataset of `int64_t`s from `start` to `stop`, stepping by `step`.
inline tensorflow::tstring CreateSerializedRangeDatasetGraphDef(int64_t start,
                                                                int64_t stop,
                                                                int64_t step) {
  tensorflow::Scope root = tensorflow::Scope::NewRootScope();
  tensorflow::ops::internal::RangeDataset dataset(
      root, /*start=*/tensorflow::ops::Const(root, start),
      /*stop=*/tensorflow::ops::Const(root, stop),
      /*step=*/tensorflow::ops::Const(root, step),
      /*output_types=*/{tensorflow::DT_INT64},
      /*output_shapes=*/{tensorflow::TensorShape({})});
  tensorflow::ops::internal::DatasetToGraphV2 graph_def_tensor(root, dataset);
  tensorflow::ClientSession session(root);
  std::vector<tensorflow::Tensor> outputs;
  auto status = session.Run(/*fetch_outputs=*/{graph_def_tensor}, &outputs);
  tensorflow::tstring graph_def = outputs[0].flat<tensorflow::tstring>()(0);
  return graph_def;
}

// Returns a value representing a sequence of `int64_t`s from `start` to `stop`,
// stepping by `step`.
inline v0::Value SequenceV(int64_t start, int64_t stop, int64_t step) {
  tensorflow::tstring sequence_graph =
      CreateSerializedRangeDatasetGraphDef(start, stop, step);
  v0::Value value_proto;
  v0::Value::Sequence* sequence_pb = value_proto.mutable_sequence();
  *sequence_pb->mutable_serialized_graph_def() =
      std::string(sequence_graph.data(), sequence_graph.size());

  v0::TensorType tensor_type;
  tensor_type.set_dtype(v0::TensorType::DT_INT64);
  tensor_type.add_dims(1);
  *sequence_pb->mutable_element_type()->mutable_tensor() = tensor_type;

  return value_proto;
}

inline absl::StatusOr<std::vector<std::vector<tensorflow::Tensor>>>
SequenceValueToList(const v0::Value::Sequence& sequence) {
  std::unique_ptr<tensorflow::data::standalone::Dataset> dataset =
      TFF_TRY(SequenceValueToDataset(sequence));
  std::unique_ptr<tensorflow::data::standalone::Iterator> iterator;
  tensorflow::Status status = dataset->MakeIterator(&iterator);
  if (!status.ok()) {
    return absl::InternalError(
        absl::StrCat("Unable to make iterator from sequence dataset: ",
                     status.error_message()));
  }
  std::vector<std::vector<tensorflow::Tensor>> outputs;
  while (true) {
    bool end_of_input;
    std::vector<tensorflow::Tensor> output;
    status = iterator->GetNext(&output, &end_of_input);
    if (!status.ok()) {
      return absl::InternalError(
          absl::StrCat("Failed to get the ", outputs.size(),
                       "th element of the sequence: ", status.error_message()));
    }
    if (end_of_input) {
      break;
    }
    outputs.push_back(std::move(output));
  }
  return outputs;
}

MATCHER(TensorsProtoEqual,
        absl::StrCat(negation ? "aren't" : "are",
                     " tensors equal under proto comparison")) {
  const tensorflow::Tensor& first = std::get<0>(arg);
  const tensorflow::Tensor& second = std::get<1>(arg);
  tensorflow::TensorProto first_proto;
  first.AsProtoTensorContent(&first_proto);
  tensorflow::TensorProto second_proto;
  second.AsProtoTensorContent(&second_proto);
  return testing::EqualsProto(second_proto)
      .impl()
      .MatchAndExplain(first_proto, result_listener);
}

namespace intrinsic {

#define INTRINSIC_FUNC(name, uri) \
  inline v0::Value name() { return IntrinsicV(#uri); }

INTRINSIC_FUNC(ArgsIntoSequenceV, args_into_sequence);
INTRINSIC_FUNC(FederatedAggregateV, federated_aggregate);
INTRINSIC_FUNC(FederatedBroadcastV, federated_broadcast);
INTRINSIC_FUNC(FederatedMapV, federated_map);
INTRINSIC_FUNC(FederatedMapAllEqualV, federated_map_all_equal);
INTRINSIC_FUNC(FederatedEvalAtClientsV, federated_eval_at_clients);
INTRINSIC_FUNC(FederatedEvalAtServerV, federated_eval_at_server);
INTRINSIC_FUNC(FederatedSelectV, federated_select);
INTRINSIC_FUNC(FederatedValueAtClientsV, federated_value_at_clients);
INTRINSIC_FUNC(FederatedValueAtServerV, federated_value_at_server);
INTRINSIC_FUNC(FederatedZipAtClientsV, federated_zip_at_clients);
INTRINSIC_FUNC(FederatedZipAtServerV, federated_zip_at_server);

#undef INTRINSIC_FUNC

}  // namespace intrinsic

inline v0::Value ComputationV(v0::Computation computation_pb) {
  v0::Value value_pb;
  *value_pb.mutable_computation() = computation_pb;
  return value_pb;
}

inline v0::Computation SelectionComputation(v0::Computation source_pb,
                                            int32_t index) {
  v0::Computation computation_pb;
  v0::Selection* selection_pb = computation_pb.mutable_selection();
  *selection_pb->mutable_source() = source_pb;
  selection_pb->set_index(index);
  return computation_pb;
}

inline v0::Computation StructComputation(
    std::vector<v0::Computation> elements) {
  v0::Computation computation_pb;
  v0::Struct* struct_pb = computation_pb.mutable_struct_();
  for (const auto& element : elements) {
    v0::Struct::Element* element_pb = struct_pb->add_element();
    *element_pb->mutable_value() = element;
  }
  return computation_pb;
}

inline v0::Computation LambdaComputation(
    absl::optional<absl::string_view> parameter_name,
    v0::Computation result_computation_value) {
  v0::Computation computation_pb;
  v0::Lambda* lambda_pb = computation_pb.mutable_lambda();
  if (parameter_name != absl::nullopt) {
    lambda_pb->mutable_parameter_name()->assign(parameter_name.value().data(),
                                                parameter_name.value().size());
  }
  *lambda_pb->mutable_result() = result_computation_value;
  return computation_pb;
}

inline v0::Computation BlockComputation(
    std::vector<std::tuple<std::string, v0::Computation>> locals,
    v0::Computation result) {
  v0::Computation computation_pb;
  v0::Block* block_pb = computation_pb.mutable_block();
  for (const auto& local : locals) {
    v0::Block::Local* new_local_pb = block_pb->add_local();
    *new_local_pb->mutable_name() = std::get<0>(local);
    *new_local_pb->mutable_value() = std::get<1>(local);
  }
  *block_pb->mutable_result() = result;
  return computation_pb;
}

inline v0::Computation ReferenceComputation(absl::string_view reference_name) {
  v0::Computation computation_pb;
  computation_pb.mutable_reference()->mutable_name()->assign(
      reference_name.data(), reference_name.size());
  return computation_pb;
}

inline v0::Computation IntrinsicComputation(absl::string_view uri) {
  v0::Computation computation_pb;
  computation_pb.mutable_intrinsic()->mutable_uri()->assign(uri.data(),
                                                            uri.size());
  return computation_pb;
}

inline v0::Computation DataComputation(absl::string_view uri) {
  v0::Computation computation_pb;
  computation_pb.mutable_data()->mutable_uri()->assign(uri.data(), uri.size());
  return computation_pb;
}

inline v0::Computation PlacementComputation(absl::string_view uri) {
  v0::Computation computation_pb;
  computation_pb.mutable_placement()->mutable_uri()->assign(uri.data(),
                                                            uri.size());
  return computation_pb;
}

}  // namespace testing
}  // namespace tensorflow_federated

#endif  // THIRD_PARTY_TENSORFLOW_FEDERATED_CC_CORE_IMPL_EXECUTORS_VALUE_TEST_UTILS_H_
