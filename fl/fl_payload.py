#!/usr/bin/env python3
"""WFL1 payload codec shared by the Python FL driver and Weaver C++ apps."""

from __future__ import annotations

import struct
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List


MAGIC = b"WFL1"
HEADER = struct.Struct("!4sII")


@dataclass
class FlPayload:
    sample_count: int
    values: List[float]


def encode_payload(payload: FlPayload) -> bytes:
    body = struct.pack(f"!{len(payload.values)}d", *payload.values) if payload.values else b""
    return HEADER.pack(MAGIC, int(payload.sample_count), len(payload.values)) + body


def decode_payload(data: bytes) -> FlPayload:
    if len(data) < HEADER.size:
        raise ValueError("payload is too short")
    magic, sample_count, dim = HEADER.unpack_from(data)
    if magic != MAGIC:
        raise ValueError("invalid WFL1 magic")
    expected = HEADER.size + dim * 8
    if len(data) != expected:
        raise ValueError(f"invalid WFL1 size: expected {expected}, got {len(data)}")
    values = list(struct.unpack_from(f"!{dim}d", data, HEADER.size)) if dim else []
    return FlPayload(sample_count=sample_count, values=values)


def write_payload(path: str | Path, payload: FlPayload) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(encode_payload(payload))


def read_payload(path: str | Path) -> FlPayload:
    return decode_payload(Path(path).read_bytes())


def weighted_average(payloads: Iterable[FlPayload]) -> FlPayload:
    total_samples = 0
    weighted_sum: List[float] = []

    for payload in payloads:
        if not weighted_sum:
            weighted_sum = [0.0] * len(payload.values)
        if len(payload.values) != len(weighted_sum):
            raise ValueError("payload dimensions do not match")
        samples = max(1, int(payload.sample_count))
        total_samples += samples
        for i, value in enumerate(payload.values):
            weighted_sum[i] += value * samples

    if total_samples == 0:
        raise ValueError("cannot average zero payloads")

    return FlPayload(
        sample_count=total_samples,
        values=[value / total_samples for value in weighted_sum],
    )

