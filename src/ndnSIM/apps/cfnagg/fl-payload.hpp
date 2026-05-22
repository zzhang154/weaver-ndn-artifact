#ifndef CFNAGG_FL_PAYLOAD_HPP
#define CFNAGG_FL_PAYLOAD_HPP

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace cfnagg {

struct FlPayload
{
  uint32_t sampleCount = 0;
  std::vector<double> values;
};

std::vector<uint8_t>
EncodeFlPayload(const FlPayload& payload);

bool
DecodeFlPayload(const uint8_t* data, size_t size, FlPayload& payload);

bool
ReadFlPayloadFile(const std::string& path, std::vector<uint8_t>& bytes);

bool
WriteFlPayloadFile(const std::string& path, const std::vector<uint8_t>& bytes);

std::string
BuildNodeRoundPayloadPath(const std::string& directory, const std::string& nodeName, int seq);

std::string
NormalizeNodeName(const std::string& prefixOrName);

} // namespace cfnagg

#endif // CFNAGG_FL_PAYLOAD_HPP
