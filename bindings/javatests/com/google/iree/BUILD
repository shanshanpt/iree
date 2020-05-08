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

load("//tools/build_defs/android:rules.bzl", "android_binary", "android_instrumentation_test", "android_library")

package(
    default_visibility = ["//visibility:public"],
    licenses = ["notice"],  # Apache 2.0
)

android_library(
    name = "ContextTestLib",
    testonly = 1,
    srcs = ["ContextTest.java"],
    tags = [
        "noga",
    ],
    deps = [
        "//bindings/java/com/google/iree:Context",
        "//third_party/android/androidx_test/ext/junit",
        "//third_party/java/junit:junit-android",
    ],
)

android_binary(
    name = "ContextTestApp",
    testonly = 1,
    manifest = "TestManifest.xml",
    tags = [
        "noga",
    ],
    deps = [
        ":ContextTestLib",
        "//java/com/google/android/apps/common/testing/testrunner",
    ],
)

android_instrumentation_test(
    name = "ContextTest",
    tags = [
        "noga",
    ],
    target_device = "//tools/android/emulated_devices/generic_phone:android_29_x86",
    test_app = "ContextTestApp",
)
