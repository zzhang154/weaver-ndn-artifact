#ifndef WEAVER_MININDN_FL_PAYLOAD_HPP
#define WEAVER_MININDN_FL_PAYLOAD_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace weaver {

struct FlPayload
{
  uint32_t sampleCount = 0;
  std::vector<double> values;
};

std::vector<uint8_t>
encodeFlPayload(const FlPayload& payload);

bool
decodeFlPayload(const uint8_t* data, size_t size, FlPayload& payload);

bool
readFlPayloadFile(const std::string& path, std::vector<uint8_t>& bytes);

bool
writeFlPayloadFile(const std::string& path, const std::vector<uint8_t>& bytes);

std::string
buildNodeRoundPayloadPath(const std::string& directory, const std::string& nodeName, uint64_t seq);

} // namespace weaver

#endif // WEAVER_MININDN_FL_PAYLOAD_HPP
