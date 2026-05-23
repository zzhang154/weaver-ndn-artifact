# Weaver miniNDN Version

This directory is the miniNDN/ndn-cxx version of the Weaver core.

It mirrors the ndnSIM implementation at `../src/ndnSIM/apps/cfnagg/`, but runs as real ndn-cxx processes on Mini-NDN hosts:

- `producer`: registers a prefix and returns a numeric value for each sequence.
- `aggregator`: registers a prefix, forwards each parent Interest to direct children, sums child Data, and returns one aggregated Data packet.
- `root`: initiates aggregation rounds, maintains a congestion window, logs the final aggregate, and stops when all rounds complete.

## Source Layout

- `weaverapps/`
  - `weaverd`: one binary with `--role producer|aggregator|root`
  - `aggregation-buffer.hpp`: per-sequence partial aggregation state
  - `fl-payload.*`: WFL1 serialized FL model/update payload codec
  - `quic-packet.*`: QUIC aggregation packet envelope codec for MiniNDN-FL payload compatibility
  - `congestion-control.*`: AIMD, CUBIC, simplified BBR
  - `weaver-node.*`: Root/Aggregator/Producer process logic
  - `trace-collector.*`: CSV event tracing
- `topologies/weaver-simple.conf`
  - `con0 -> agg0, agg1 -> pro0..pro3`
- `examples/weaver_simple.py`
  - Starts NFD/NLSR and launches the Weaver roles on Mini-NDN hosts.

## Build

```bash
cd miniNDN/weaverapps
make
```

## Run Example

The example uses Mini-NDN's Python package and starts a complete emulation.

```bash
sudo -E env PYTHONPATH=/path/to/mini-ndn:/path/to/mini-ndn/dl/mininet \
  python3 miniNDN/examples/weaver_simple.py
```

The root trace is written to:

```text
miniNDN/logs/con0-trace.csv
```

## Manual Commands

Producer:

```bash
./weaverd --role producer --prefix /pro0 --payload-dir /tmp/weaver-fl/payloads
```

To carry the same model update inside the ns-3 QUIC aggregation packet envelope, add:

```bash
--payload-format quic
```

Aggregator:

```bash
./weaverd --role aggregator --prefix /agg0 --children /pro0,/pro1
```

Root:

```bash
./weaverd --role root --prefix /con0 --children /agg0,/agg1 --iterations 100 --cc AIMD --output-dir /tmp/weaver-fl/outputs
```

Supported root congestion controls:

- `AIMD`
- `CUBIC`
- `BBR`

The BBR controller is intentionally the same style as the ndnSIM core: simplified and aggregation-round based, not full TCP BBR.

For automated FL runs, `examples/weaver_simple.py` also reads these environment variables:

- `WEAVER_PAYLOAD_DIR`: producer input WFL1 directory
- `WEAVER_OUTPUT_DIR`: root aggregate output directory
- `WEAVER_PAYLOAD_FORMAT`: `wfl1` or `quic`
- `WEAVER_ITERATIONS`: root aggregation rounds
- `WEAVER_RUNTIME`: seconds to wait before stopping
- `WEAVER_NO_CLI=1`: skip the interactive Mini-NDN CLI
