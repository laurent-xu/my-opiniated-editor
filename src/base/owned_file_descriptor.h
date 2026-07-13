#pragma once

#include <unistd.h>

#include <utility>

#include "src/base/file_descriptor.h"

namespace moe::base {

class OwnedFileDescriptor {
 public:
  explicit OwnedFileDescriptor(FileDescriptor const descriptor_value = FileDescriptor{})
      : descriptor(descriptor_value) {}

  OwnedFileDescriptor(OwnedFileDescriptor const&) = delete;
  OwnedFileDescriptor& operator=(OwnedFileDescriptor const&) = delete;

  OwnedFileDescriptor(OwnedFileDescriptor&& other) noexcept
      : descriptor(std::exchange(other.descriptor, FileDescriptor{})) {}

  OwnedFileDescriptor& operator=(OwnedFileDescriptor&& other) noexcept {
    if (this != &other) {
      reset();
      descriptor = std::exchange(other.descriptor, FileDescriptor{});
    }
    return *this;
  }

  ~OwnedFileDescriptor() { reset(); }

  [[nodiscard]] FileDescriptor get() const { return descriptor; }
  [[nodiscard]] bool valid() const { return descriptor.is_valid(); }

  void reset(FileDescriptor const next_descriptor = FileDescriptor{}) noexcept {
    if (descriptor.is_valid()) {
      ::close(descriptor.value());
    }
    descriptor = next_descriptor;
  }

 private:
  FileDescriptor descriptor;
};

}  // namespace moe::base
