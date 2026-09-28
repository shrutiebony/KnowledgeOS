#!/usr/bin/env python3
"""Unsupervised GraphSAGE-style mean aggregation (2 layers).

This writes neighborhood-aware representations. It does NOT output an AI
percentage and must not be treated as a detector without labeled validation.
"""
from __future__ import annotations

import argparse
from collections import defaultdict


def load_nodes(path: str) -> dict[int, list[float]]:
    nodes: dict[int, list[float]] = {}
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            parts = line.strip().split("\t")
            if len(parts) < 2:
                continue
            nodes[int(parts[0])] = [float(x) for x in parts[1:]]
    return nodes


def load_edges(path: str) -> dict[int, list[int]]:
    adj: dict[int, list[int]] = defaultdict(list)
    with open(path, encoding="utf-8") as fh:
        for line in fh:
            parts = line.strip().split()
            if len(parts) < 2:
                continue
            src, dst = int(parts[0]), int(parts[1])
            adj[src].append(dst)
            adj[dst].append(src)
    return adj


def aggregate(features: dict[int, list[float]], adj: dict[int, list[int]]) -> dict[int, list[float]]:
    out: dict[int, list[float]] = {}
    dim = len(next(iter(features.values())))
    for node, feat in features.items():
        acc = feat[:]
        neighbors = adj.get(node, [])
        for nb in neighbors:
            nf = features.get(nb)
            if not nf:
                continue
            for i in range(dim):
                acc[i] += nf[i]
        denom = 1 + len(neighbors)
        out[node] = [v / denom for v in acc]
    return out


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--nodes", required=True)
    parser.add_argument("--edges", required=True)
    parser.add_argument("--out", required=True)
    args = parser.parse_args()

    h0 = load_nodes(args.nodes)
    if not h0:
        raise SystemExit("no nodes")
    adj = load_edges(args.edges)
    h1 = aggregate(h0, adj)
    h2 = aggregate(h1, adj)

    with open(args.out, "w", encoding="utf-8") as fh:
        for node in sorted(h2):
            row = "\t".join(f"{v:.6f}" for v in h2[node])
            fh.write(f"{node}\t{row}\n")


if __name__ == "__main__":
    main()
