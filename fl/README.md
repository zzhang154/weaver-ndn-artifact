# Weaver FL Driver

This is the first FL-ready integration layer, modeled after the ns3-fl split:

- Python owns the FL data/model loop.
- Weaver owns the NDN transport and in-network aggregation.
- Serialized model updates are real binary payloads, not scalar placeholders.

## Payload Format

Client updates and root aggregates use `WFL1`:

- 4-byte magic: `WFL1`
- uint32 sample count
- uint32 vector dimension
- big-endian float64 values

The matching C++ codec is `src/ndnSIM/apps/cfnagg/fl-payload.*`.

## Weaver NDN Runs

These configs run the FL loop over the Weaver NDN backends. The smoke configs keep `network.execute=false`, so they locally perform the same weighted average without launching waf or MiniNDN.

```bash
cd fl
python3 weaver_fl.py --config configs/ndnsim_smoke.json
python3 weaver_fl.py --config configs/minindn_smoke.json
```

To launch a real network backend, set:

```json
"execute": true
```

For ndnSIM, apply this overlay to a buildable ndnSIM tree before running `./waf`. For MiniNDN, provide a command that starts the MiniNDN experiment. The driver exports `WEAVER_PAYLOAD_DIR`, `WEAVER_OUTPUT_DIR`, `WEAVER_SEQ`, `WEAVER_PRODUCERS`, and `WEAVER_PAYLOAD_FORMAT`.

## Optional QUIC Baseline

The ns-3 QUIC aggregation backend is kept as a baseline, not the main Weaver path:

```bash
python3 weaver_fl.py --config configs/ns3_quic_smoke.json
python3 weaver_fl.py --config configs/ns3_quic_exec.example.json
```
