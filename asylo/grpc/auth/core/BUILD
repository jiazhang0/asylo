#
# Copyright 2018 Asylo authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

licenses(["notice"])  # Apache v2.0

# Description:
#   This package provides an implementation of gRPC security components for the
#   enclave gRPC security stack.

# Used to selectively enable gtest tests to run inside an enclave.
load("//asylo/bazel:asylo.bzl", "cc_test")
load("//asylo/bazel:copts.bzl", "ASYLO_DEFAULT_COPTS")
load("//asylo/bazel:proto.bzl", "asylo_proto_library")
load("@io_bazel_rules_go//go:def.bzl", "go_binary")

package(default_visibility = [
    "//asylo/grpc/auth:__subpackages__",
])

# Implementation of credentials, security connectors, and transport security
# interface (TSI) for enclave gRPC security stack.
cc_library(
    name = "grpc_security_enclave",
    srcs = [
        "enclave_credentials.cc",
        "enclave_security_connector.cc",
        "enclave_transport_security.cc",
    ],
    hdrs = [
        "enclave_credentials.h",
        "enclave_grpc_security_constants.h",
        "enclave_security_connector.h",
        "enclave_transport_security.h",
    ],
    copts = ASYLO_DEFAULT_COPTS,
    deps = [
        ":assertion_description",
        ":client_ekep_handshaker",
        ":ekep_handshaker",
        ":ekep_handshaker_util",
        ":enclave_credentials_options",
        ":handshake_proto_cc",
        ":server_ekep_handshaker",
        "//asylo/grpc/auth/util:safe_string",
        "//asylo/identity:identity_proto_cc",
        "//asylo/util:cleansing_types",
        "//asylo/util:status",
        "@com_github_grpc_grpc//:alts_frame_protector",
        "@com_github_grpc_grpc//:gpr_base",
        "@com_github_grpc_grpc//:grpc_base_c",
        "@com_github_grpc_grpc//:grpc_secure",
        "@com_github_grpc_grpc//:tsi_interface",
        "@com_google_absl//absl/memory",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

# Channel and server credentials options.
cc_library(
    name = "enclave_credentials_options",
    srcs = ["enclave_credentials_options.cc"],
    hdrs = ["enclave_credentials_options.h"],
    deps = [
        ":assertion_description",
        "//asylo/grpc/auth/util:safe_string",
    ],
)

# Defines a C structure analogous to the AssertionDescription proto.
cc_library(
    name = "assertion_description",
    srcs = ["assertion_description.cc"],
    hdrs = ["assertion_description.h"],
    deps = [
        "//asylo/grpc/auth/util:safe_string",
        "@com_github_grpc_grpc//:gpr_base",
    ],
)

cc_test(
    name = "assertion_description_test",
    srcs = ["assertion_description_test.cc"],
    enclave_test_name = "assertion_description_enclave_test",
    tags = ["regression"],
    deps = [
        ":assertion_description",
        "//asylo/identity:identity_proto_cc",
        "//asylo/test/util:test_main",
        "@com_google_googletest//:gtest",
    ],
)

# Utility for managing hashed EKEP transcripts.
cc_library(
    name = "transcript",
    srcs = ["transcript.cc"],
    hdrs = ["transcript.h"],
    visibility = ["//visibility:private"],
    deps = [
        "//asylo/crypto:hash_interface",
        "//asylo/crypto/util:byte_container_util",
        "//asylo/util:status",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

# Tests for EKEP transcripts.
cc_test(
    name = "transcript_test",
    srcs = ["transcript_test.cc"],
    enclave_test_name = "transcript_enclave_test",
    tags = ["regression"],
    deps = [
        ":transcript",
        "//asylo/crypto:hash_interface",
        "//asylo/crypto:sha256_hash",
        "//asylo/crypto/util:byte_container_view",
        "//asylo/test/util:status_matchers",
        "//asylo/test/util:test_main",
        "//asylo/util:status",
        "@com_google_absl//absl/memory",
        "@com_google_googletest//:gtest",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

# Error space implementation for EKEP Abort error codes.
cc_library(
    name = "ekep_error_space",
    srcs = ["ekep_error_space.cc"],
    hdrs = ["ekep_error_space.h"],
    deps = [
        ":handshake_proto_cc",
        "//asylo/util:status",
    ],
)

# Implementation of EKEP cryptographic operations.
cc_library(
    name = "ekep_crypto",
    srcs = ["ekep_crypto.cc"],
    hdrs = ["ekep_crypto.h"],
    deps = [
        ":ekep_error_space",
        ":handshake_proto_cc",
        "//asylo/crypto/util:bssl_util",
        "//asylo/crypto/util:byte_container_view",
        "//asylo/util:cleansing_types",
        "//asylo/util:logging",
        "//asylo/util:status",
        "@boringssl//:crypto",
        "@com_google_absl//absl/strings",
    ],
)

# Tests for the EKEP crypto library.
cc_test(
    name = "ekep_crypto_test",
    srcs = ["ekep_crypto_test.cc"],
    enclave_test_name = "ekep_crypto_enclave_test",
    tags = ["regression"],
    deps = [
        ":ekep_crypto",
        ":ekep_error_space",
        ":handshake_proto_cc",
        "//asylo/crypto/util:bytes",
        "//asylo/crypto/util:trivial_object_util",
        "//asylo/test/util:status_matchers",
        "//asylo/test/util:test_main",
        "@boringssl//:crypto",
        "@com_google_googletest//:gtest",
    ],
)

# Implementation of the Enclave Key Exchange Protocol (EKEP) handshake.
cc_library(
    name = "ekep_handshaker",
    srcs = ["ekep_handshaker.cc"],
    hdrs = ["ekep_handshaker.h"],
    copts = ASYLO_DEFAULT_COPTS,
    deps = [
        ":ekep_crypto",
        ":ekep_error_space",
        ":handshake_proto_cc",
        ":transcript",
        "//asylo/crypto:hash_interface",
        "//asylo/crypto/util:bssl_util",
        "//asylo/crypto/util:byte_container_view",
        "//asylo/grpc/auth/util:multi_buffer_input_stream",
        "//asylo/identity:identity_proto_cc",
        "//asylo/identity/null_identity:null_assertion_generator",
        "//asylo/identity/null_identity:null_assertion_verifier",
        "//asylo/util:cleansing_types",
        "//asylo/util:logging",
        "//asylo/util:status",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@com_google_protobuf//:protobuf_lite",
    ],
)

# Server implementation of EkepHandshaker.
cc_library(
    name = "server_ekep_handshaker",
    srcs = ["server_ekep_handshaker.cc"],
    hdrs = ["server_ekep_handshaker.h"],
    deps = [
        ":ekep_crypto",
        ":ekep_error_space",
        ":ekep_handshaker",
        ":ekep_handshaker_util",
        ":handshake_proto_cc",
        "//asylo/crypto:sha256_hash",
        "//asylo/identity:identity_proto_cc",
        "//asylo/util:cleansing_types",
        "//asylo/util:logging",
        "//asylo/util:status",
        "@boringssl//:crypto",
        "@com_google_absl//absl/memory",
        "@com_google_protobuf//:protobuf",
    ],
)

# Client implementation of EkepHandshaker.
cc_library(
    name = "client_ekep_handshaker",
    srcs = ["client_ekep_handshaker.cc"],
    hdrs = ["client_ekep_handshaker.h"],
    deps = [
        ":ekep_crypto",
        ":ekep_error_space",
        ":ekep_handshaker",
        ":ekep_handshaker_util",
        ":handshake_proto_cc",
        "//asylo/crypto:sha256_hash",
        "//asylo/identity:identity_proto_cc",
        "//asylo/util:cleansing_types",
        "//asylo/util:logging",
        "//asylo/util:status",
        "@boringssl//:crypto",
        "@com_google_absl//absl/memory",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
    ],
)

# Utilities used by EkepHandshaker implementations.
cc_library(
    name = "ekep_handshaker_util",
    srcs = ["ekep_handshaker_util.cc"],
    hdrs = ["ekep_handshaker_util.h"],
    deps = [
        ":ekep_handshaker",
        "//asylo/identity:enclave_assertion_authority",
        "//asylo/identity:enclave_assertion_generator",
        "//asylo/identity:enclave_assertion_verifier",
        "//asylo/identity:identity_proto_cc",
        "//asylo/util:status",
        "@com_google_absl//absl/strings",
    ],
)

# Tests for EKEP handshaker utility functions.
cc_test(
    name = "ekep_handshaker_util_test",
    srcs = ["ekep_handshaker_util_test.cc"],
    enclave_test_name = "ekep_handshaker_util_enclave_test",
    tags = ["regression"],
    deps = [
        ":ekep_handshaker",
        ":ekep_handshaker_util",
        "//asylo/identity:identity_proto_cc",
        "//asylo/identity/null_identity:null_identity_util",
        "//asylo/test/util:status_matchers",
        "//asylo/test/util:test_main",
        "@com_google_googletest//:gtest",
    ],
)

# Definition of Enclave Key Exchange Protocol (EKEP) handshake messages.
asylo_proto_library(
    name = "handshake_proto",
    srcs = ["handshake.proto"],
    deps = ["//asylo/identity:identity_proto"],
)

# Generates test vectors for EKEP secret derivation.
go_binary(
    name = "generate_test_vectors",
    srcs = ["generate_test_vectors.go"],
    deps = [
        "@com_github_golang_crypto//:curve25519",
        "@com_github_golang_crypto//:hkdf",
    ],
)
