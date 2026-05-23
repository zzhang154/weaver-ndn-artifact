# Weaver Core Artifact

This directory is a lightweight core overlay for the Weaver prototype. It keeps the Weaver-owned ndnSIM application code and the minimum scenario/build files needed to run the artifact. It intentionally does not vendor the full ns-3, ndnSIM, NFD, or ndn-cxx trees.

## What Is Included

- `src/ndnSIM/apps/cfnagg/`
  - Weaver-owned NDN applications and support code:
    - `CFNRootApp`
    - `CFNAggregatorApp`
    - `CFNProducerApp`
    - aggregation buffer
    - AIMD/CUBIC/simplified-BBR congestion control
    - straggler timeout manager
    - trace collector
- `src/ndnSIM/examples/cfnagg/`
  - Weaver simulation entry point, build file, and example topologies.
- `src/ndnSIM/examples/wscript`
  - ndnSIM examples build hook with `bld.recurse('cfnagg')`.
- `scripts/`
  - local debug/rebuild helper scripts.
- `miniNDN/`
  - miniNDN/ndn-cxx implementation of the same Weaver Root/Aggregator/Producer design.
  - builds one process binary, `weaverd`, with `--role root|aggregator|producer`.
- `fl/`
  - Python FL driver/config layer in the style of ns3-fl.
  - serializes real model updates into WFL1 payloads and calls the Weaver network backend.
  - primary backends are the Weaver ndnSIM and MiniNDN implementations; ns-3 QUIC is included only as an optional baseline.

## What Is Excluded

These are framework/runtime dependencies, not Weaver core code:

- ns-3 framework files
- upstream ndnSIM model/helper/utils/apps examples
- NFD
- ndn-cxx
- Waf caches and Python bytecode
- runtime logs and packet traces
- the design prompt under `src/ndnSIM/prompt/`

## Overlay Usage

Apply this overlay to an ns-3.35 + ndnSIM 2.9 tree:

```bash
rsync -a src/ndnSIM/apps/cfnagg/ \
  /path/to/ns-3/src/ndnSIM/apps/cfnagg/

rsync -a src/ndnSIM/examples/cfnagg/ \
  /path/to/ns-3/src/ndnSIM/examples/cfnagg/
```

Then add this line to the target `src/ndnSIM/examples/wscript` if it is not already present:

```python
bld.recurse('cfnagg')
```

Build and run from the ns-3 root:

```bash
./waf configure --disable-python --enable-examples -d debug
./waf
./waf --run "cfnagg-simulation --topology=src/ndnSIM/examples/cfnagg/topologies/dcn.txt --aggTree=src/ndnSIM/examples/cfnagg/topologies/aggtree-dcn.txt --cc=AIMD --simTime=10.0 --logFile=log_file/cfnagg-trace.csv"
```

## miniNDN Usage

Build the miniNDN binary:

```bash
cd miniNDN/weaverapps
make
```

Run the included Mini-NDN example:

```bash
sudo python3 miniNDN/examples/weaver_simple.py
```

See `miniNDN/README.md` for the process-level commands and trace locations.

## FL Usage

Run the first-stage FL smoke loop:

```bash
cd fl
python3 weaver_fl.py --config configs/ndnsim_smoke.json
python3 weaver_fl.py --config configs/minindn_smoke.json
```

The smoke configs use the same WFL1 serialized model-update files as the Weaver NDN backends, but keep `network.execute=false` so the training loop can be checked without launching waf or MiniNDN. Set `execute=true` after applying the overlay to a buildable ndnSIM tree or configuring the MiniNDN command.

Optional baseline configs for the ns-3 QUIC aggregation tree are also provided:

```bash
python3 weaver_fl.py --config configs/ns3_quic_smoke.json
python3 weaver_fl.py --config configs/ns3_quic_exec.example.json
```

## Source Notes

The repository contains only the artifact overlay and intentionally omits local workstation paths, private remotes, generated build products, packet traces, and runtime logs.
