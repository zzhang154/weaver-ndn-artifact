#include "fl-payload.hpp"

#include <arpa/inet.h>

#include <cstring>
#include <fstream>
#include <limits>
#include <iterator>
#include <sstream>
#include <utility>

namespace cfnagg {

namespace {

constexpr uint8_t MAGIC[] = {'W', 'F', 'L', '1'};
constexpr size_t HEADER_SIZE = 12;

uint64_t
HostToBe64(uint64_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(value);
#else
  return value;
#endif
}

uint64_t
Be64ToHost(uint64_t value)
{
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
  return __builtin_bswap64(value);
#else
  return value;
#endif
}

void
AppendU32(std::vector<uint8_t>& out, uint32_t value)
{
  uint32_t net = htonl(value);
  const auto* p = reinterpret_cast<const uint8_t*>(&net);
  out.insert(out.end(), p, p + sizeof(net));
}

bool
ReadU32(const uint8_t*& p, const uint8_t* end, uint32_t& value)
{
  if (static_cast<size_t>(end - p) < sizeof(uint32_t)) {
    return false;
  }
  uint32_t net = 0;
  std::memcpy(&net, p, sizeof(net));
  p += sizeof(net);
  value = ntohl(net);
  return true;
}

void
AppendDouble(std::vector<uint8_t>& out, double value)
{
  static_assert(sizeof(double) == sizeof(uint64_t), "double must be 64-bit");
  uint64_t bits = 0;
  std::memcpy(&bits, &value, sizeof(bits));
  bits = HostToBe64(bits);
  const auto* p = reinterpret_cast<const uint8_t*>(&bits);
  out.insert(out.end(), p, p + sizeof(bits));
}

bool
ReadDouble(const uint8_t*& p, const uint8_t* end, double& value)
{
  if (static_cast<size_t>(end - p) < sizeof(uint64_t)) {
    return false;
  }
  uint64_t bits = 0;
  std::memcpy(&bits, p, sizeof(bits));
  p += sizeof(bits);
  bits = Be64ToHost(bits);
  std::memcpy(&value, &bits, sizeof(value));
  return true;
}

} // namespace

std::vector<uint8_t>
EncodeFlPayload(const FlPayload& payload)
{
  std::vector<uint8_t> out;
  out.reserve(HEADER_SIZE + payload.values.size() * sizeof(double));
  out.insert(out.end(), std::begin(MAGIC), std::end(MAGIC));
  AppendU32(out, payload.sampleCount);
  AppendU32(out, static_cast<uint32_t>(payload.values.size()));

  for (double value : payload.values) {
    AppendDouble(out, value);
  }
  return out;
}

bool
DecodeFlPayload(const uint8_t* data, size_t size, FlPayload& payload)
{
  if (data == nullptr || size < HEADER_SIZE || std::memcmp(data, MAGIC, sizeof(MAGIC)) != 0) {
    return false;
  }

  const uint8_t* p = data + sizeof(MAGIC);
  const uint8_t* end = data + size;
  uint32_t sampleCount = 0;
  uint32_t dim = 0;
  if (!ReadU32(p, end, sampleCount) || !ReadU32(p, end, dim)) {
    return false;
  }
  if (dim > (std::numeric_limits<size_t>::max() / sizeof(double))) {
    return false;
  }
  if (static_cast<size_t>(end - p) != static_cast<size_t>(dim) * sizeof(double)) {
    return false;
  }

  FlPayload decoded;
  decoded.sampleCount = sampleCount;
  decoded.values.reserve(dim);
  for (uint32_t i = 0; i < dim; ++i) {
    double value = 0.0;
    if (!ReadDouble(p, end, value)) {
      return false;
    }
    decoded.values.push_back(value);
  }

  payload = std::move(decoded);
  return true;
}

bool
ReadFlPayloadFile(const std::string& path, std::vector<uint8_t>& bytes)
{
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return false;
  }
  input.seekg(0, std::ios::end);
  const auto size = input.tellg();
  input.seekg(0, std::ios::beg);
  bytes.resize(static_cast<size_t>(size));
  if (!bytes.empty()) {
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  return static_cast<bool>(input) || input.eof();
}

bool
WriteFlPayloadFile(const std::string& path, const std::vector<uint8_t>& bytes)
{
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  if (!bytes.empty()) {
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
  }
  return static_cast<bool>(output);
}

std::string
NormalizeNodeName(const std::string& prefixOrName)
{
  if (!prefixOrName.empty() && prefixOrName.front() == '/') {
    return prefixOrName.substr(1);
  }
  return prefixOrName;
}

std::string
BuildNodeRoundPayloadPath(const std::string& directory, const std::string& nodeName, int seq)
{
  std::ostringstream os;
  os << directory;
  if (!directory.empty() && directory.back() != '/') {
    os << '/';
  }
  os << NormalizeNodeName(nodeName) << '-' << seq << ".wfl";
  return os.str();
}

} // namespace cfnagg
