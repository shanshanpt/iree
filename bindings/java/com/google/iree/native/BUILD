# Copyright 2020 Google LLC
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

load("//mobile/build:build_defs.bzl", "android_jni_library")

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],  # Apache 2.0
)

android_jni_library(
    name = "native_deps",
    srcs = [
        "context_jni.cc",
    ],
    binary_name = "libiree.so",
    tags = [
        "noga",
    ],
    deps = [
        "//iree/base:logging",
        # TODO(jennik): Update this for OSS side.
        "//third_party/java/jdk:jni",
        ":context",
    ],
)

cc_library(
    name = "context",
    srcs = ["context_wrapper.cc"],
    hdrs = ["context_wrapper.h"],
    tags = [
        "noga",
    ],
    deps = [
        "//iree/base:api",
        "//iree/base:logging",
        "//iree/base:status",
        "//iree/vm:context",
    ],
)
