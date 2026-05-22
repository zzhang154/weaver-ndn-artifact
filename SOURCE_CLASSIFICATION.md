# Source Classification

This file records the boundary between Weaver-owned code and external framework/runtime code.

## Weaver-Owned Core

ndnSIM application logic:

- `src/ndnSIM/apps/cfnagg/aggregation-buffer.hpp`
- `src/ndnSIM/apps/cfnagg/cfn-aggregator-app.cpp`
- `src/ndnSIM/apps/cfnagg/cfn-aggregator-app.hpp`
- `src/ndnSIM/apps/cfnagg/cfn-producer-app.cpp`
- `src/ndnSIM/apps/cfnagg/cfn-producer-app.hpp`
- `src/ndnSIM/apps/cfnagg/cfn-root-app.cpp`
- `src/ndnSIM/apps/cfnagg/cfn-root-app.hpp`
- `src/ndnSIM/apps/cfnagg/congestion-aimd.cpp`
- `src/ndnSIM/apps/cfnagg/congestion-bbr.cpp`
- `src/ndnSIM/apps/cfnagg/congestion-control.hpp`
- `src/ndnSIM/apps/cfnagg/congestion-cubic.cpp`
- `src/ndnSIM/apps/cfnagg/fl-payload.cpp`
- `src/ndnSIM/apps/cfnagg/fl-payload.hpp`
- `src/ndnSIM/apps/cfnagg/straggler-manager.cpp`
- `src/ndnSIM/apps/cfnagg/straggler-manager.hpp`
- `src/ndnSIM/apps/cfnagg/trace-collector.cpp`
- `src/ndnSIM/apps/cfnagg/trace-collector.hpp`

ndnSIM scenario and build overlay:

- `src/ndnSIM/examples/cfnagg/cfnagg-simulation.cpp`
- `src/ndnSIM/examples/cfnagg/wscript`
- `src/ndnSIM/examples/cfnagg/topologies/aggtree-dcn.txt`
- `src/ndnSIM/examples/cfnagg/topologies/dcn.txt`
- `src/ndnSIM/examples/wscript`

Mini-NDN implementation:

- `miniNDN/weaverapps/aggregation-buffer.hpp`
- `miniNDN/weaverapps/congestion-control.cpp`
- `miniNDN/weaverapps/congestion-control.hpp`
- `miniNDN/weaverapps/fl-payload.cpp`
- `miniNDN/weaverapps/fl-payload.hpp`
- `miniNDN/weaverapps/main.cpp`
- `miniNDN/weaverapps/trace-collector.cpp`
- `miniNDN/weaverapps/trace-collector.hpp`
- `miniNDN/weaverapps/weaver-node.cpp`
- `miniNDN/weaverapps/weaver-node.hpp`
- `miniNDN/weaverapps/Makefile`
- `miniNDN/examples/weaver_simple.py`
- `miniNDN/topologies/weaver-simple.conf`

FL integration layer:

- `fl/fl_payload.py`
- `fl/weaver_fl.py`
- `fl/configs/*.json`
- `fl/README.md`

## External Dependencies Excluded

The following are runtime or framework dependencies and are not part of this source archive:

- ns-3 framework files
- upstream ndnSIM framework, helper, model, and example code
- Mini-NDN itself
- NFD, NLSR, and ndn-cxx
- Waf caches, build outputs, packet traces, runtime logs, generated WFL1 payloads, and Python bytecode

## Notes

- `congestion-bbr.cpp` and the Mini-NDN BBR implementation are simplified aggregation-round controllers, not full TCP BBR state machines.
- `src/ndnSIM/examples/wscript` is an overlay helper. When applying this artifact to an existing ndnSIM checkout, merge the `bld.recurse('cfnagg')` line rather than blindly overwriting local edits.
