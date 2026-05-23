#!/usr/bin/env python3
"""Small FL driver for Weaver network backends.

The shape follows ns3-fl: Python owns FL data/model logic, and a network
backend transports/aggregates serialized client updates.
"""

from __future__ import annotations

import argparse
import json
import math
import os
import random
import shlex
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, List, Sequence, Tuple

from fl_payload import FlPayload, read_payload, weighted_average, write_payload


Vector = List[float]
Example = Tuple[Vector, int]


@dataclass
class ClientState:
    name: str
    data: List[Example]


def dot(a: Sequence[float], b: Sequence[float]) -> float:
    return sum(x * y for x, y in zip(a, b))


def sigmoid(x: float) -> float:
    if x >= 0:
        z = math.exp(-x)
        return 1.0 / (1.0 + z)
    z = math.exp(x)
    return z / (1.0 + z)


def make_synthetic_clients(config: Dict) -> List[ClientState]:
    data_cfg = config["data"]
    rng = random.Random(data_cfg.get("seed", 7))
    num_clients = int(data_cfg["num_clients"])
    samples_per_client = int(data_cfg["samples_per_client"])
    dim = int(data_cfg["dim"])
    skew = float(data_cfg.get("client_bias_skew", 0.0))
    client_names = data_cfg.get("client_names")
    if client_names is not None:
        if len(client_names) != num_clients:
            raise ValueError("data.client_names length must equal data.num_clients")
        client_names = [str(name) for name in client_names]

    true_w = [rng.gauss(0.0, 1.0) for _ in range(dim)]
    clients = []
    for client_idx in range(num_clients):
        client_rng = random.Random(data_cfg.get("seed", 7) + 1009 * client_idx)
        bias = skew * (client_idx - (num_clients - 1) / 2.0)
        examples = []
        for _ in range(samples_per_client):
            x = [client_rng.gauss(bias, 1.0) for _ in range(dim)]
            score = dot(true_w, x) + 0.1 * client_rng.gauss(0.0, 1.0)
            y = 1 if score >= 0.0 else 0
            examples.append((x, y))
        name = client_names[client_idx] if client_names is not None else f"pro{client_idx}"
        clients.append(ClientState(name=name, data=examples))
    return clients


def gradient(weights: Vector, data: Sequence[Example]) -> Vector:
    dim = len(weights) - 1
    grad = [0.0] * len(weights)
    for x, y in data:
        logit = dot(weights[:dim], x) + weights[-1]
        err = sigmoid(logit) - y
        for i in range(dim):
            grad[i] += err * x[i]
        grad[-1] += err
    scale = 1.0 / max(1, len(data))
    return [g * scale for g in grad]


def local_train(weights: Vector, data: Sequence[Example], lr: float, epochs: int) -> Vector:
    local = list(weights)
    for _ in range(epochs):
        grad = gradient(local, data)
        local = [w - lr * g for w, g in zip(local, grad)]
    return local


def accuracy(weights: Vector, clients: Sequence[ClientState]) -> float:
    dim = len(weights) - 1
    correct = 0
    total = 0
    for client in clients:
        for x, y in client.data:
            pred = 1 if sigmoid(dot(weights[:dim], x) + weights[-1]) >= 0.5 else 0
            correct += int(pred == y)
            total += 1
    return correct / max(1, total)


def local_weighted_average(payload_dir: Path, output_dir: Path, producers: Sequence[str], seq: int) -> Path:
    payloads = [read_payload(payload_dir / f"{producer}-{seq}.wfl") for producer in producers]
    aggregate = weighted_average(payloads)
    output_path = output_dir / f"aggregate-{seq}.wfl"
    write_payload(output_path, aggregate)
    return output_path


def run_ndnsim_backend(config: Dict, payload_dir: Path, output_dir: Path, producers: Sequence[str], seq: int) -> Path:
    network = config["network"]
    output_dir.mkdir(parents=True, exist_ok=True)

    if not network.get("execute", False):
        return local_weighted_average(payload_dir, output_dir, producers, seq)

    ns3_root = Path(network["ns3_root"])
    waf = ns3_root / "waf"
    if not waf.exists():
        raise FileNotFoundError(f"cannot find waf at {waf}")

    run_arg = (
        f"cfnagg-simulation "
        f"--topology={network['topology']} "
        f"--aggTree={network['agg_tree']} "
        f"--cc={network.get('cc', 'AIMD')} "
        f"--simTime={network.get('sim_time', 20.0)} "
        f"--logFile={output_dir / 'cfnagg-trace.csv'} "
        f"--flPayloadDir={payload_dir} "
        f"--flOutputDir={output_dir} "
        f"--maxSeq={seq}"
    )
    subprocess.run([str(waf), "--run", run_arg], cwd=ns3_root, check=True)

    output_path = output_dir / f"aggregate-{seq}.wfl"
    if not output_path.exists():
        raise FileNotFoundError(f"ndnSIM backend did not produce {output_path}")
    return output_path


def run_minindn_backend(config: Dict, payload_dir: Path, output_dir: Path, producers: Sequence[str], seq: int) -> Path:
    network = config["network"]
    output_dir.mkdir(parents=True, exist_ok=True)

    if not network.get("execute", False):
        return local_weighted_average(payload_dir, output_dir, producers, seq)

    command = network.get("command")
    if not command:
        raise ValueError("miniNDN backend execute=true requires network.command")

    env = os.environ.copy()
    env["WEAVER_PAYLOAD_DIR"] = str(payload_dir)
    env["WEAVER_OUTPUT_DIR"] = str(output_dir)
    env["WEAVER_SEQ"] = str(seq)
    env["WEAVER_ITERATIONS"] = str(seq)
    env["WEAVER_RUNTIME"] = str(network.get("runtime", 60))
    env["WEAVER_NO_CLI"] = "1"
    env["WEAVER_PRODUCERS"] = ",".join(producers)
    env["WEAVER_PAYLOAD_FORMAT"] = str(network.get("payload_format", "wfl1"))
    subprocess.run(command, shell=True, check=True, env=env)

    output_path = output_dir / f"aggregate-{seq}.wfl"
    if not output_path.exists():
        raise FileNotFoundError(f"miniNDN backend did not produce {output_path}")
    summary = {
        "backend": "minindn",
        "executed_network": True,
        "payload_format": env["WEAVER_PAYLOAD_FORMAT"],
        "aggregate_source": "minindn_weaver_data_plane",
    }
    (output_dir / "minindn-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return output_path


def run_ns3_quic_backend(config: Dict, payload_dir: Path, output_dir: Path, producers: Sequence[str], seq: int) -> Path:
    """Run the ns-3 QUIC aggregation backend.

    When WEAVER_PAYLOAD_DIR/WEAVER_OUTPUT_DIR are present, the patched QUIC
    data plane reads WFL1 model updates from producer files, carries them as
    QUIC payload bytes, aggregates them at in-network servers, and writes the
    root WFL1 aggregate.
    """

    network = config["network"]
    output_dir.mkdir(parents=True, exist_ok=True)

    if not network.get("execute", False):
        return local_weighted_average(payload_dir, output_dir, producers, seq)

    output_path = output_dir / f"aggregate-{seq}.wfl"
    first_payload = read_payload(payload_dir / f"{producers[0]}-{seq}.wfl")
    payload_dim = len(first_payload.values)
    wfl_payload_bytes = 12 + payload_dim * 8
    wfl_vsize = math.ceil(wfl_payload_bytes / 8)

    env = os.environ.copy()
    env["WEAVER_PAYLOAD_DIR"] = str(payload_dir)
    env["WEAVER_OUTPUT_DIR"] = str(output_dir)
    env["WEAVER_SEQ"] = str(seq)
    env["WEAVER_PRODUCERS"] = ",".join(producers)

    pcap_dir = network.get("pcap_dir")
    if pcap_dir:
        Path(pcap_dir).mkdir(parents=True, exist_ok=True)

    log_path = output_dir / "ns3-quic.log"
    cwd = Path(network.get("cwd", "."))
    if "command" in network:
        command = str(network["command"])
        shell = True
    else:
        binary = Path(network["binary"])
        args = [str(binary)]
        if network.get("pass_standard_args", True):
            configured_vsize = network.get("vsize")
            vsize = max(int(configured_vsize), wfl_vsize) if configured_vsize is not None else wfl_vsize
            args.extend(
                [
                    f"--itr={int(network.get('iterations', seq))}",
                    f"--vsize={vsize}",
                    f"--topotype={int(network.get('topotype', 0))}",
                    f"--cc={network.get('cc', 'bbr')}",
                    f"--stoptime={network.get('stop_time', 5)}",
                ]
            )
            if "basetime" in network:
                args.append(f"--basetime={network['basetime']}")
        args.extend(str(arg) for arg in network.get("args", []))
        command = args
        shell = False

    with log_path.open("w") as log_file:
        if isinstance(command, str):
            log_file.write(f"$ {command}\n")
        else:
            log_file.write("$ " + " ".join(shlex.quote(part) for part in command) + "\n")
        log_file.flush()
        subprocess.run(
            command,
            cwd=cwd,
            env=env,
            shell=shell,
            check=True,
            stdout=log_file,
            stderr=subprocess.STDOUT,
        )

    if output_path.exists():
        summary = {
            "backend": "ns3_quic",
            "executed_network": True,
            "network_log": str(log_path),
            "aggregate_source": "ns3_quic_wfl1_data_plane",
            "wfl_payload_bytes": wfl_payload_bytes,
        }
        (output_dir / "ns3-quic-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
        return output_path

    if not network.get("local_aggregate_after_execute", True):
        raise FileNotFoundError(f"ns-3 QUIC backend did not produce {output_path}")

    aggregate_path = local_weighted_average(payload_dir, output_dir, producers, seq)
    summary = {
        "backend": "ns3_quic",
        "executed_network": True,
        "network_log": str(log_path),
        "aggregate_source": "local_weighted_average_after_ns3_quic_run",
        "note": "The ns-3 QUIC backend did not write a WFL1 aggregate; this run used the compatibility fallback.",
    }
    (output_dir / "ns3-quic-summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    return aggregate_path


def run_round(config: Dict, round_idx: int, weights: Vector, clients: Sequence[ClientState]) -> Vector:
    training = config["training"]
    network = config["network"]
    seq = int(network.get("seq", 1))

    run_dir = Path(config["run_dir"]) / f"round-{round_idx:04d}"
    if run_dir.exists():
        shutil.rmtree(run_dir)
    payload_dir = run_dir / "payloads"
    output_dir = run_dir / "outputs"
    payload_dir.mkdir(parents=True)

    producers = []
    for client in clients:
        local_model = local_train(
            weights,
            client.data,
            lr=float(training["local_lr"]),
            epochs=int(training.get("local_epochs", 1)),
        )
        producers.append(client.name)
        write_payload(
            payload_dir / f"{client.name}-{seq}.wfl",
            FlPayload(sample_count=len(client.data), values=local_model),
        )

    backend = network.get("backend", "ndnsim")
    if backend == "ndnsim":
        aggregate_path = run_ndnsim_backend(config, payload_dir, output_dir, producers, seq)
    elif backend == "minindn":
        aggregate_path = run_minindn_backend(config, payload_dir, output_dir, producers, seq)
    elif backend == "ns3_quic":
        aggregate_path = run_ns3_quic_backend(config, payload_dir, output_dir, producers, seq)
    else:
        raise ValueError(f"unsupported backend: {backend}")
    return read_payload(aggregate_path).values


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    args = parser.parse_args()

    config = json.loads(Path(args.config).read_text())
    clients = make_synthetic_clients(config)
    dim = int(config["data"]["dim"])
    weights = [0.0] * (dim + 1)

    rounds = int(config["training"]["rounds"])
    print("round,accuracy,weights_l2")
    for round_idx in range(1, rounds + 1):
        weights = run_round(config, round_idx, weights, clients)
        l2 = math.sqrt(sum(w * w for w in weights))
        print(f"{round_idx},{accuracy(weights, clients):.6f},{l2:.6f}")

    final_path = Path(config["run_dir"]) / "final-model.wfl"
    write_payload(final_path, FlPayload(sample_count=sum(len(c.data) for c in clients), values=weights))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
