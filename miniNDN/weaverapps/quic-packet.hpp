#ifndef WEAVER_MININDN_QUIC_PACKET_HPP
#define WEAVER_MININDN_QUIC_PACKET_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace weaver {

std::vector<uint8_t>
encodeQuicAggregationPacket(uint16_t iteration, const std::vector<uint8_t>& payload,
                            size_t payloadCapacity = 0);

bool
decodeQuicAggregationPacket(const uint8_t* data, size_t size, std::vector<uint8_t>& payload,
                            uint16_t* iteration = nullptr);

bool
hasQuicAggregationHeader(const uint8_t* data, size_t size);

} // namespace weaver

#endif // WEAVER_MININDN_QUIC_PACKET_HPP
