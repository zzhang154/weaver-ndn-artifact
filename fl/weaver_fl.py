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
    subprocess.run(command, shell=True, check=True, env=env)

    output_path = output_dir / f"aggregate-{seq}.wfl"
    if not output_path.exists():
        raise FileNotFoundError(f"miniNDN backend did not produce {output_path}")
    return output_path


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
