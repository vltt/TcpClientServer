#pragma once

#include <stdint.h>

namespace models {

#pragma pack(push, 1)

struct TcpPacketRequest {
  uint32_t num;
  bool is_last;
  char data[4 * 1024 - sizeof(uint32_t) - sizeof(bool)];
};

#pragma pack(pop)

}  // namespace models
