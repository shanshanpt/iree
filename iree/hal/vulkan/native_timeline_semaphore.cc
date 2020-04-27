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

#include "iree/hal/vulkan/native_timeline_semaphore.h"

#include "iree/base/tracing.h"
#include "iree/hal/vulkan/status_util.h"

namespace iree {
namespace hal {
namespace vulkan {

NativeTimelineSemaphore::NativeTimelineSemaphore(
    ref_ptr<VkDeviceHandle> logical_device, VkSemaphore handle,
    uint64_t initial_value)
    : logical_device_(std::move(logical_device)), handle_(handle) {
  IREE_TRACE_SCOPE0("NativeTimelineSemaphore::ctor");
}

NativeTimelineSemaphore::~NativeTimelineSemaphore() {
  IREE_TRACE_SCOPE0("NativeTimelineSemaphore::dtor");
  logical_device_->syms()->vkDestroySemaphore(*logical_device_, handle_,
                                              logical_device_->allocator());
}

StatusOr<uint64_t> NativeTimelineSemaphore::Query() {
  uint64_t value = 0;
  VK_RETURN_IF_ERROR(logical_device_->syms()->vkGetSemaphoreCounterValue(
      *logical_device_, handle_, &value));
  if (value == UINT64_MAX) {
    absl::MutexLock lock(&status_mutex_);
    return status_;
  }
  return value;
}

Status NativeTimelineSemaphore::Signal(uint64_t value) {
  IREE_TRACE_SCOPE0("NativeTimelineSemaphore::Signal");

  VkSemaphoreSignalInfo signal_info;
  signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
  signal_info.pNext = nullptr;
  signal_info.semaphore = handle_;
  signal_info.value = value;
  return VkResultToStatus(logical_device_->syms()->vkSignalSemaphore(
      *logical_device_, &signal_info));
}

void NativeTimelineSemaphore::Fail(Status status) {
  IREE_TRACE_SCOPE0("NativeTimelineSemaphore::Fail");

  // NOTE: we hold the lock here as the vkSignalSemaphore may wake a waiter and
  // we want to be able to immediately give them the status.
  absl::MutexLock lock(&status_mutex_);
  status_ = std::move(status);

  VkSemaphoreSignalInfo signal_info;
  signal_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
  signal_info.pNext = nullptr;
  signal_info.semaphore = handle_;
  signal_info.value = UINT64_MAX;
  // NOTE: we don't care about the result in case of failures as we are
  // failing and the caller will likely be tearing everything down anyway.
  logical_device_->syms()->vkSignalSemaphore(*logical_device_, &signal_info);
}

Status NativeTimelineSemaphore::Wait(uint64_t value, absl::Time deadline) {
  IREE_TRACE_SCOPE0("NativeTimelineSemaphore::Wait");

  VkSemaphoreWaitInfo wait_info;
  wait_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
  wait_info.pNext = nullptr;
  wait_info.flags = 0;
  wait_info.semaphoreCount = 1;
  wait_info.pSemaphores = &handle_;
  wait_info.pValues = &value;

  uint64_t timeout_nanos;
  if (deadline == absl::InfiniteFuture()) {
    timeout_nanos = UINT64_MAX;
  } else if (deadline == absl::InfinitePast()) {
    timeout_nanos = 0;
  } else {
    auto relative_nanos = absl::ToInt64Nanoseconds(deadline - absl::Now());
    timeout_nanos = relative_nanos < 0 ? 0 : relative_nanos;
  }

  // NOTE: this may fail with a timeout (VK_TIMEOUT) or in the case of a
  // device loss event may return either VK_SUCCESS *or* VK_ERROR_DEVICE_LOST.
  // We may want to explicitly query for device loss after a successful wait
  // to ensure we consistently return errors.
  VkResult result = logical_device_->syms()->vkWaitSemaphores(
      *logical_device_, &wait_info, timeout_nanos);
  if (result == VK_ERROR_DEVICE_LOST) {
    // Nothing we do now matters.
    return VkResultToStatus(result);
  } else if (result == VK_TIMEOUT) {
    return DeadlineExceededErrorBuilder(IREE_LOC)
           << "Deadline exceeded waiting for semaphore";
  }

  return VkResultToStatus(result);
}

}  // namespace vulkan
}  // namespace hal
}  // namespace iree
