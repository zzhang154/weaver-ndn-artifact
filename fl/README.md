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

## Smoke Run

This runs the FL loop and uses the same WFL1 files that the ndnSIM backend consumes. The sample config keeps `network.execute=false`, so it locally performs the same weighted average without launching waf.

```bash
cd fl
python3 weaver_fl.py --config configs/ndnsim_smoke.json
```

To launch the real ndnSIM backend, set:

```json
"execute": true
```

and apply this overlay to a buildable ns-3/ndnSIM tree before running `./waf`.

The miniNDN backend uses the same WFL1 files. Its smoke config is:

```bash
python3 weaver_fl.py --config configs/minindn_smoke.json
```

For real miniNDN execution, set `network.execute=true` and provide a command that starts the miniNDN experiment. The driver exports `WEAVER_PAYLOAD_DIR`, `WEAVER_OUTPUT_DIR`, `WEAVER_SEQ`, and `WEAVER_PRODUCERS` for that command.
