#include "weaver-node.hpp"

#include <ndn-cxx/face.hpp>
#include <ndn-cxx/security/key-chain.hpp>

#include <algorithm>
#include <cctype>
#include <exception>
#include <iostream>
#include <map>
#include <string>

namespace {

void
usage(const char* program)
{
  std::cerr
    << "Usage:\n"
    << "  " << program << " --role producer --prefix /pro0 [--value 1]\n"
    << "  " << program << " --role aggregator --prefix /agg0 --children /pro0,/pro1\n"
    << "  " << program << " --role root --prefix /con0 --children /agg0,/agg1 --iterations 100 --cc AIMD\n"
    << "\n"
    << "Options:\n"
    << "  --role <producer|aggregator|root>\n"
    << "  --prefix <name>              local node prefix\n"
    << "  --children <p1,p2,...>       direct child prefixes for root/aggregator\n"
    << "  --cc <AIMD|CUBIC|BBR>        root congestion control, default AIMD\n"
    << "  --iterations <n>             root aggregation rounds, default 100\n"
    << "  --value <n>                  producer value per round, default 1\n"
    << "  --timeout-ms <n>             straggler timeout, default 1000\n"
    << "  --lifetime-ms <n>            Interest lifetime, default 1000\n"
    << "  --freshness-ms <n>           Data freshness, default 10000\n"
    << "  --start-delay-ms <n>         root startup delay, default 1000\n"
    << "  --trace <path>               CSV trace file\n"
    << "  --payload-dir <path>         producer input WFL1 files: <producer>-<seq>.wfl\n"
    << "  --output-dir <path>          root output WFL1 files: aggregate-<seq>.wfl\n";
}

std::string
lower(std::string value)
{
  std::transform(value.begin(), value.end(), value.begin(), [] (unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

std::map<std::string, std::string>
parseArgs(int argc, char* argv[])
{
  std::map<std::string, std::string> args;
  for (int i = 1; i < argc; ++i) {
    std::string key = argv[i];
    if (key == "--help" || key == "-h") {
      args["help"] = "true";
      continue;
    }
    if (key.rfind("--", 0) != 0) {
      throw std::runtime_error("unexpected positional argument: " + key);
    }
    key = key.substr(2);
    if (i + 1 >= argc) {
      throw std::runtime_error("missing value for --" + key);
    }
    args[key] = argv[++i];
  }
  return args;
}

std::string
getString(const std::map<std::string, std::string>& args,
          const std::string& key, const std::string& fallback = "")
{
  auto it = args.find(key);
  if (it == args.end()) {
    return fallback;
  }
  return it->second;
}

uint64_t
getUint64(const std::map<std::string, std::string>& args,
          const std::string& key, uint64_t fallback)
{
  auto it = args.find(key);
  if (it == args.end()) {
    return fallback;
  }
  return std::stoull(it->second);
}

int
getInt(const std::map<std::string, std::string>& args, const std::string& key, int fallback)
{
  auto it = args.find(key);
  if (it == args.end()) {
    return fallback;
  }
  return std::stoi(it->second);
}

} // namespace

int
main(int argc, char* argv[])
{
  try {
    const auto args = parseArgs(argc, argv);
    if (args.count("help") > 0) {
      usage(argv[0]);
      return 0;
    }

    weaver::NodeOptions options;
    options.role = lower(getString(args, "role"));
    options.prefix = weaver::normalizePrefix(getString(args, "prefix"));
    options.children = weaver::splitChildren(getString(args, "children"));
    options.cc = getString(args, "cc", options.cc);
    options.traceFile = getString(args, "trace");
    options.payloadDir = getString(args, "payload-dir");
    options.outputDir = getString(args, "output-dir");
    options.value = getUint64(args, "value", options.value);
    options.iterations = getUint64(args, "iterations", options.iterations);
    options.timeoutMs = getInt(args, "timeout-ms", options.timeoutMs);
    options.interestLifetimeMs = getInt(args, "lifetime-ms", options.interestLifetimeMs);
    options.freshnessMs = getInt(args, "freshness-ms", options.freshnessMs);
    options.startDelayMs = getInt(args, "start-delay-ms", options.startDelayMs);

    if (options.role.empty()) {
      throw std::runtime_error("--role is required");
    }
    if (options.prefix == "/") {
      throw std::runtime_error("--prefix is required");
    }
    if (options.traceFile.empty()) {
      options.traceFile = "logs/weaver-" + options.role + "-" +
                          options.prefix.substr(1) + ".csv";
    }

    ndn::Face face;
    ndn::KeyChain keyChain;
    weaver::TraceCollector trace(options.traceFile);

    if (options.role == "producer") {
      weaver::ProducerNode node(options, face, keyChain, trace);
      node.run();
    }
    else if (options.role == "aggregator") {
      weaver::AggregatorNode node(options, face, keyChain, trace);
      node.run();
    }
    else if (options.role == "root") {
      weaver::RootNode node(options, face, trace);
      node.run();
    }
    else {
      throw std::runtime_error("unknown role: " + options.role);
    }

    return 0;
  }
  catch (const std::exception& e) {
    std::cerr << "ERROR: " << e.what() << "\n";
    usage(argv[0]);
    return 2;
  }
}
