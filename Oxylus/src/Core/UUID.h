#pragma once

#include <cstdint>
#include <unordered_map>

namespace Oxylus {
  class UUID {
  public:
    UUID();

    UUID(uint64_t uuid);

    UUID(const UUID&) = default;

    operator uint64_t() const {
      return m_UUID;
    }

  private:
    uint64_t m_UUID;
  };
}

template<>
struct std::hash<Oxylus::UUID> {
  size_t operator()(const Oxylus::UUID& uuid) const noexcept {
    return uuid;
  }
};
