#pragma once

#include <stdint.h>

namespace models {

#pragma pack(push, 1)

struct TcpPacketResponse {
  std::uint32_t num;
  bool done;
};

#pragma pack(pop)

}  // namespace models
