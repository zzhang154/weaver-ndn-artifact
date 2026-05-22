#include "ns3/core-module.h"
#include "ns3/network-module.h"
#include "ns3/ndnSIM-module.h"

#include "apps/cfnagg/cfn-producer-app.hpp"
#include "apps/cfnagg/cfn-aggregator-app.hpp"
#include "apps/cfnagg/cfn-root-app.hpp"
#include "apps/cfnagg/trace-collector.hpp"

#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <vector>

// ADD THIS: Define log component for this file
NS_LOG_COMPONENT_DEFINE("CFNAggregationSim");

using namespace ns3;

inline void
PrintFib(Ptr<Node> node, std::ostream &os)
{
  // Get L3Protocol from node
  Ptr<ns3::ndn::L3Protocol> l3 = node->GetObject<ns3::ndn::L3Protocol>();
  if (!l3) {
    os << "Node " << Names::FindName(node) << " has no L3Protocol installed." << std::endl;
    return;
  }
  // Get forwarder from L3Protocol
  auto forwarder = l3->getForwarder();
  if (!forwarder) {
    os << "Node " << Names::FindName(node) << " has no forwarder." << std::endl;
    return;
  }
  // Access the FIB
  nfd::fib::Fib &fib = forwarder->getFib();

  // Use iterators to access FIB entries
  os << "FIB entries for node " << Names::FindName(node) << ":" << std::endl;
  for (auto it = fib.begin(); it != fib.end(); ++it) {
    const auto &entry = *it;
    os << entry.getPrefix().toUri() << " -> ";
    const auto &nhs = entry.getNextHops();
    for (const auto &nh : nhs) {
      os << "[face " << nh.getFace().getId() 
      << ", cost " << nh.getCost() << "] ";
    }
    os << std::endl;
  }
}

int main(int argc, char* argv[]) {
  std::string topologyFile = "topologies/dcn.txt";
  std::string aggTreeFile = "topologies/aggtree-dcn.txt";
  std::string ccAlgorithm = "AIMD";
  double simTime = 20.0;
  std::string logFile = "cfnagg-trace.csv";
  std::string flPayloadDir = "";
  std::string flOutputDir = "";
  uint32_t maxSeq = 0;
  uint32_t producerSampleCount = 1;

  CommandLine cmd;
  cmd.AddValue("topology", "Path to the topology file (dcn.txt)", topologyFile);
  cmd.AddValue("aggTree", "Path to the aggregation tree file (aggtree-dcn.txt)", aggTreeFile);
  cmd.AddValue("cc", "Congestion control algorithm (AIMD, CUBIC, BBR)", ccAlgorithm);
  cmd.AddValue("simTime", "Simulation duration (seconds)", simTime);
  cmd.AddValue("logFile", "Output log file name", logFile);
  cmd.AddValue("flPayloadDir", "Directory with producer FL payload files named <producer>-<seq>.wfl", flPayloadDir);
  cmd.AddValue("flOutputDir", "Directory where root writes aggregate-<seq>.wfl payloads", flOutputDir);
  cmd.AddValue("maxSeq", "Maximum aggregation sequence to request; 0 means unlimited", maxSeq);
  cmd.AddValue("producerSampleCount", "Fallback sample count for generated producer FL payloads", producerSampleCount);
  cmd.Parse(argc, argv);

  // Read the network topology
  AnnotatedTopologyReader topoReader("", 25);
  topoReader.SetFileName(topologyFile);
  topoReader.Read();

  // Debug node names
  std::cout << "Debugging node names:" << std::endl;
  NodeContainer testNodes = NodeContainer::GetGlobal();
  for (uint32_t i = 0; i < testNodes.GetN(); i++) {
  std::cout << "Node " << i << " name: '" << Names::FindName(testNodes.Get(i)) << "'" << std::endl;
  }

  // ADD THIS: Enable NFD-level logging for debugging
  NS_LOG_INFO("Enabling detailed NFD logging");
  // Replace invalid logging components with valid ones
  ns3::LogComponentEnable("ndn-cxx.nfd.Forwarder", ns3::LOG_LEVEL_DEBUG);
  ns3::LogComponentEnable("ndn.L3Protocol", ns3::LOG_LEVEL_DEBUG);

  // Install NDN stack on all nodes
  ns3::ndn::StackHelper ndnHelper;
  ndnHelper.SetDefaultRoutes(true);  // enables forwarding strategy to default route if necessary
  
  ndnHelper.setCsSize(10000); 
  // Choose NFD content store, then set policy and size:
  // Keep the content store configuration explicit for reproducible experiments.
  ndnHelper.setPolicy("nfd::cs::lru");

  // Install stack
  ndnHelper.InstallAll();

  // For setting forwarding strategy, use the StrategyChoiceHelper instead
  ns3::ndn::StrategyChoiceHelper::InstallAll("/", "/localhost/nfd/strategy/best-route");

  // Install global routing interface on all nodes
  ns3::ndn::GlobalRoutingHelper globalRouting;
  globalRouting.InstallAll();

  // Parse the aggregation tree configuration file
  std::map<std::string, std::vector<std::string>> childrenMap;
  std::ifstream aggFile(aggTreeFile);
  if (!aggFile.is_open()) {
    std::cerr << "ERROR: Cannot open aggregation tree file: " << aggTreeFile << std::endl;
    return 1;
  }
  std::string line;
  while (std::getline(aggFile, line)) {
    if (line.size() == 0 || line[0] == '#') continue; // skip empty or comment lines
    std::stringstream ss(line);
    std::string parent;
    ss >> parent;
    std::vector<std::string> childList;
    std::string child;
    while (ss >> child) {
      childList.push_back(child);
    }
    childrenMap[parent] = childList;
  }
  aggFile.close();

  // Determine the root of the aggregation tree (the node that is never listed as a child)
  std::set<std::string> allChildren;
  for (auto& kv : childrenMap) {
    for (auto& child : kv.second) {
      allChildren.insert(child);
    }
  }
  std::string rootName;
  for (auto& kv : childrenMap) {
    const std::string& node = kv.first;
    if (allChildren.find(node) == allChildren.end()) {
      rootName = node;
      break;
    }
  }
  if (rootName.empty()) {
    std::cerr << "ERROR: Root node not identified in aggregation tree\n";
    return 1;
  }

  // Identify all nodes involved in the aggregation tree (union of all parents and children)
  std::set<std::string> allNodes;
  for (auto& kv : childrenMap) {
    allNodes.insert(kv.first);
    for (auto& child : kv.second) {
      allNodes.insert(child);
    }
  }

  // Identify leaf nodes (those that do not have children of their own in the map)
  std::vector<std::string> leaves;
  for (const std::string& node : allNodes) {
    if (childrenMap.find(node) == childrenMap.end()) {
      // Node not present as a key in map means it has no children => leaf
      leaves.push_back(node);
    }
  }

  // Install root application on the root node
  Ptr<Node> rootNode = Names::Find<Node>(rootName);
  ns3::ndn::AppHelper rootHelper("ns3::CFNRootApp");
  rootHelper.SetAttribute("Prefix", StringValue("/" + rootName));
  rootHelper.SetAttribute("CongestionControl", StringValue(ccAlgorithm));
  rootHelper.SetAttribute("MaxSeq", UintegerValue(maxSeq));
  rootHelper.SetAttribute("FlOutputDir", StringValue(flOutputDir));
  rootHelper.Install(rootNode);
  Ptr<CFNRootApp> rootApp = DynamicCast<CFNRootApp>(rootNode->GetApplication(0));

  // Install aggregator applications on intermediate nodes (all parents except root)
  std::map<std::string, Ptr<CFNAggregatorApp>> aggAppMap;
  for (auto& kv : childrenMap) {
    const std::string& parent = kv.first;
    if (parent == rootName) continue;
    Ptr<Node> parentNode = Names::Find<Node>(parent);
    ns3::ndn::AppHelper aggHelper("ns3::CFNAggregatorApp");
    aggHelper.SetAttribute("Prefix", StringValue("/" + parent));
    aggHelper.Install(parentNode);
    Ptr<CFNAggregatorApp> aggApp = DynamicCast<CFNAggregatorApp>(parentNode->GetApplication(0));
    aggAppMap[parent] = aggApp;
  }

  // Install producer applications on all leaf nodes
  for (const std::string& leaf : leaves) {
    Ptr<Node> leafNode = Names::Find<Node>(leaf);
    ns3::ndn::AppHelper producerHelper("ns3::CFNProducerApp");
    producerHelper.SetAttribute("Prefix", StringValue("/" + leaf));
    producerHelper.SetAttribute("PayloadFormat", StringValue(flPayloadDir.empty() ? "legacy" : "fl"));
    producerHelper.SetAttribute("FlPayloadDir", StringValue(flPayloadDir));
    producerHelper.SetAttribute("SampleCount", UintegerValue(producerSampleCount));
    producerHelper.Install(leafNode);
    // (Optional: set different payload or value via attributes if desired)
  }

  // Configure each aggregator and the root with their children list
  for (auto& kv : childrenMap) {
    const std::string& parent = kv.first;
    const std::vector<std::string>& childList = kv.second;
    if (parent == rootName) {
      if (rootApp) {
        // Debug statement for root app
        std::cout << "DEBUG: Setting children for Root [" << parent << "]: ";
        for (const auto& child : childList) {
          std::cout << child << " ";
        }
        std::cout << std::endl;
        
        rootApp->SetChildren(childList);
        rootApp->SetCongestionControl(ccAlgorithm);
      }
    } else {
      if (aggAppMap.find(parent) != aggAppMap.end()) {
        // Debug statement for aggregator app
        std::cout << "DEBUG: Setting children for Aggregator [" << parent << "]: ";
        for (const auto& child : childList) {
          std::cout << child << " ";
        }
        std::cout << std::endl;
        
        aggAppMap[parent]->SetChildren(childList);
      }
    }
  }

  // Add origin (producer) prefixes to the global routing controller for all nodes
  for (const std::string& nodeName : allNodes) {
    Ptr<Node> node = Names::Find<Node>(nodeName);
    if (node != nullptr) {
      globalRouting.AddOrigins("/" + nodeName, node);
    }
  }

  // Calculate and install FIB routes
  globalRouting.CalculateRoutes();

  // Add explicit FIB entries for all parent-child relationships
  for (auto& kv : childrenMap) {
    const std::string& parent = kv.first;
    Ptr<Node> parentNode = Names::Find<Node>(parent);
    
    for (const auto& child : kv.second) {
      // Add a direct route from parent to child
      std::cout << "Adding explicit route: " << parent << " -> /" << child << std::endl;
      ns3::ndn::FibHelper::AddRoute(parentNode, "/" + child, Names::Find<Node>(child), 1);
      
      // Add reverse route for data packets
      std::cout << "Adding explicit route: " << child << " -> /" << parent << std::endl;
      ns3::ndn::FibHelper::AddRoute(Names::Find<Node>(child), "/" + parent, parentNode, 1);
    }
  }

  // After adding all routes
  Simulator::Schedule(MilliSeconds(100), [rootNode]() {
    std::cout << "DEBUG: Printing FIB entries on Root after delay:" << std::endl;
    PrintFib(rootNode, std::cout);
  });

  Ptr<Node> agg1Node = Names::Find<Node>("Agg1");

  // After adding all routes
  Simulator::Schedule(MilliSeconds(100), [agg1Node]() {
    std::cout << "DEBUG: Printing FIB entries on Agg1 after delay:" << std::endl;
    PrintFib(agg1Node, std::cout);
  });

  // Add these lines just before Simulator::Run()
  ns3::ndn::L3RateTracer::InstallAll("packet-trace.txt", Seconds(0.5));
  ns3::ndn::AppDelayTracer::InstallAll("app-delays-trace.txt");
  
  // Open the trace log file
  TraceCollector::Open(logFile);

  Simulator::Stop(Seconds(simTime));
  Simulator::Run();
  Simulator::Destroy();

  // Close the trace log
  TraceCollector::Close();

  return 0;
}
