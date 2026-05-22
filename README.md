# Weaver NDN Core

This repository contains the source code used for the anonymous review artifact. It keeps only the Weaver-owned implementation and experiment glue. It does not vendor ns-3, ndnSIM, Mini-NDN, NFD, NLSR, or ndn-cxx.

Public dependencies:

- ndnSIM: https://github.com/named-data-ndnSIM/ndnSIM
- Mini-NDN: https://github.com/named-data/mini-ndn

## Repository Layout

- `src/ndnSIM/apps/cfnagg/`
  - ndnSIM applications and support code: root, aggregator, producer, aggregation buffer, AIMD/CUBIC/simplified BBR controllers, straggler timeout handling, traces, and WFL1 FL payload serialization.
- `src/ndnSIM/examples/cfnagg/`
  - ndnSIM example driver, build file, aggregation tree, and small test topology.
- `miniNDN/`
  - Mini-NDN/ndn-cxx implementation of the same root/aggregator/producer design.
  - Builds one process binary, `weaverd`, with `--role root|aggregator|producer`.
- `fl/`
  - Python FL driver in the style of ns3-fl: Python owns the model/data loop, while the network backend transports and aggregates serialized model updates.
- `scripts/`
  - Helper scripts for applying the ndnSIM overlay and debugging a local ndnSIM run.

## What Is Not Included

The repository intentionally excludes:

- ns-3 framework files
- upstream ndnSIM model/helper/utils/examples
- Mini-NDN itself
- NFD, NLSR, and ndn-cxx source trees
- build outputs, caches, logs, traces, and generated WFL1 payloads

## Apply The ndnSIM Overlay

Install ndnSIM in an external ns-3 tree, then apply the Weaver overlay:

```bash
./scripts/apply_ndnsim_overlay.sh /path/to/ns-3
```

The script copies:

```text
src/ndnSIM/apps/cfnagg/     -> /path/to/ns-3/src/ndnSIM/apps/cfnagg/
src/ndnSIM/examples/cfnagg/ -> /path/to/ns-3/src/ndnSIM/examples/cfnagg/
```

It also ensures that the target `src/ndnSIM/examples/wscript` recurses into `cfnagg`.

From the ns-3 root:

```bash
./waf configure --disable-python --enable-examples -d debug
./waf
./waf --run "cfnagg-simulation --topology=src/ndnSIM/examples/cfnagg/topologies/dcn.txt --aggTree=src/ndnSIM/examples/cfnagg/topologies/aggtree-dcn.txt --cc=AIMD --simTime=10.0 --logFile=log_file/cfnagg-trace.csv"
```

## Build The miniNDN App

```bash
cd miniNDN/weaverapps
make
```

Run the included Mini-NDN example from the repository root:

```bash
sudo -E env PYTHONPATH=/path/to/mini-ndn:/path/to/mini-ndn/dl/mininet \
  python3 miniNDN/examples/weaver_simple.py
```

## Run FL Smoke Tests

The smoke configs use WFL1 serialized model-update files but keep `network.execute=false`, so they check the FL loop without launching waf or Mini-NDN:

```bash
cd fl
python3 weaver_fl.py --config configs/ndnsim_smoke.json
python3 weaver_fl.py --config configs/minindn_smoke.json
```

For real backends, copy one of the `.example.json` files, edit the external paths, and set `network.execute=true`.
