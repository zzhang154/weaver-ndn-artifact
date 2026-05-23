#include "quic-packet.hpp"

#include <algorithm>
#include <cstring>

namespace weaver {

namespace {

constexpr uint8_t QUIC_AGG_DATA_HEADER = 3;
constexpr size_t QUIC_AGG_HEADER_SIZE = 10;
constexpr uint8_t WFL_MAGIC[] = {'W', 'F', 'L', '1'};
constexpr size_t WFL_HEADER_SIZE = 12;

uint32_t
readU32Be(const uint8_t* data)
{
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

size_t
inferPayloadSize(const uint8_t* payload, size_t size)
{
  if (payload != nullptr && size >= WFL_HEADER_SIZE &&
      std::memcmp(payload, WFL_MAGIC, sizeof(WFL_MAGIC)) == 0) {
    const uint32_t dim = readU32Be(payload + 8);
    const size_t expected = WFL_HEADER_SIZE + static_cast<size_t>(dim) * sizeof(double);
    if (expected <= size) {
      return expected;
    }
  }

  while (size > 0 && payload[size - 1] == 0) {
    --size;
  }
  return size;
}

} // namespace

std::vector<uint8_t>
encodeQuicAggregationPacket(uint16_t iteration, const std::vector<uint8_t>& payload,
                            size_t payloadCapacity)
{
  const size_t contentSize = std::max(payload.size(), payloadCapacity);
  std::vector<uint8_t> packet(QUIC_AGG_HEADER_SIZE + contentSize, 0);
  std::fill(packet.begin(), packet.begin() + 8, QUIC_AGG_DATA_HEADER);
  packet[8] = static_cast<uint8_t>((iteration >> 8) & 0xff);
  packet[9] = static_cast<uint8_t>(iteration & 0xff);
  std::copy(payload.begin(), payload.end(), packet.begin() + QUIC_AGG_HEADER_SIZE);
  return packet;
}

bool
hasQuicAggregationHeader(const uint8_t* data, size_t size)
{
  if (data == nullptr || size < QUIC_AGG_HEADER_SIZE) {
    return false;
  }
  for (size_t i = 0; i < 8; ++i) {
    if (data[i] != QUIC_AGG_DATA_HEADER) {
      return false;
    }
  }
  return true;
}

bool
decodeQuicAggregationPacket(const uint8_t* data, size_t size, std::vector<uint8_t>& payload,
                            uint16_t* iteration)
{
  if (!hasQuicAggregationHeader(data, size)) {
    return false;
  }

  if (iteration != nullptr) {
    *iteration = (static_cast<uint16_t>(data[8]) << 8) | static_cast<uint16_t>(data[9]);
  }

  const uint8_t* payloadBegin = data + QUIC_AGG_HEADER_SIZE;
  const size_t payloadCapacity = size - QUIC_AGG_HEADER_SIZE;
  const size_t payloadSize = inferPayloadSize(payloadBegin, payloadCapacity);
  payload.assign(payloadBegin, payloadBegin + payloadSize);
  return true;
}

} // namespace weaver
