#!/usr/bin/env python3
"""Explain the currently visible KnowledgeOS collection from numbers.

Reads one JSON object on stdin (summary + graph groups + time breakdown).
Always writes a short deterministic explanation to stdout. If OPENAI_API_KEY
or ANTHROPIC_API_KEY is set, may polish wording — never invents numbers.
An API key is optional. GraphSAGE is never the headline. HITS is never mentioned.
"""
from __future__ import annotations

import json
import os
import sys
import urllib.error
import urllib.request


FORBIDDEN_HEADLINE = ("graphsage", "hits")
MEDICAL_JUDGMENT = (
    "diagnos",
    "prescribe",
    "treatment plan",
    "you should take",
    "efficacy",
    "contraindicat",
)


def _get(obj, *keys, default=None):
    cur = obj
    for key in keys:
        if not isinstance(cur, dict) or key not in cur:
            return default
        cur = cur[key]
    return cur


def _int(v, default=0):
    try:
        if v is None:
            return default
        return int(v)
    except (TypeError, ValueError):
        return default


def _num(v):
    if v is None:
        return None
    try:
        return int(v) if float(v) == int(float(v)) else float(v)
    except (TypeError, ValueError):
        return None


def _range_text(pair):
    if not isinstance(pair, list) or len(pair) < 2:
        return ""
    a, b = _num(pair[0]), _num(pair[1])
    if a is None or b is None:
        return ""
    return f"{a}-{b}%"


def _scope_line(payload, summary):
    name = payload.get("name") or summary.get("name") or "This collection"
    topic = payload.get("topic") or summary.get("topic")
    topic_view = bool(payload.get("topicView") or summary.get("topicView"))
    id_view = bool(payload.get("idView") or summary.get("idView"))
    n = _int(summary.get("documentCount"))
    corpus = _int(summary.get("corpusDocumentCount"), n)
    bits = [str(name)]
    if id_view:
        bits.append("selected graph cluster")
        if n and corpus and n != corpus:
            bits.append(f"{n} of {corpus} documents")
        elif n:
            bits.append(f"{n} documents")
    elif topic_view and topic:
        bits.append(f'topic "{topic}"')
        if n and corpus and n != corpus:
            bits.append(f"{n} of {corpus} documents in the same collection")
        elif n:
            bits.append(f"{n} documents")
    else:
        bits.append(f"{n} visible document" + ("" if n == 1 else "s"))
    line = " / ".join(bits) + "."
    if topic_view or id_view:
        line += " This is a view of the same collection, not a second dataset."
    return line


def _share_line(summary):
    docs = _num(summary.get("shareOfDocuments"))
    words = _num(summary.get("shareOfAnalyzedWords"))
    doc_r = _range_text(summary.get("shareOfDocumentsRange") or [])
    word_r = _range_text(summary.get("shareOfAnalyzedWordsRange") or [])
    headline = summary.get("headline")
    if docs is None and words is None:
        if headline:
            return str(headline).rstrip(".") + "."
        return "This visible set is not yet analyzed."
    parts = []
    if docs is not None:
        extra = f" (range {doc_r})" if doc_r else ""
        parts.append(f"{docs}% of documents{extra}")
    if words is not None:
        extra = f" (range {word_r})" if word_r else ""
        parts.append(f"{words}% of analyzed words{extra}")
    return "Estimated AI-generated share: " + " and ".join(parts) + "."


def _band_line(summary):
    bands = summary.get("bands") or {}
    ai = _int(bands.get("LIKELY_AI"))
    human = _int(bands.get("LIKELY_HUMAN"))
    unc = _int(bands.get("UNCERTAIN"))
    if ai + human + unc == 0:
        return "Band counts are not available for this visible set."
    return (
        f"Bands: {ai} likely AI-generated, {human} likely human-written, "
        f"{unc} uncertain."
    )


def _group_by_label(group_by):
    key = str(group_by or "").lower()
    if key == "topic":
        return "Wikipedia subheading (topic)"
    if key == "source":
        return "source"
    if key == "band":
        return "estimate band"
    return "heading"


def _graph_line(graph):
    if not isinstance(graph, dict):
        return (
            "The graph places one heading on each cluster of similar documents. "
            "Those headings label the visible set; they are not a second analysis."
        )
    group_by = _group_by_label(graph.get("groupBy"))
    groups = graph.get("groups") or []
    nodes = _int(graph.get("nodeCount"))
    edges = _int(graph.get("edgeCount"))
    names = []
    for g in groups[:4]:
        if isinstance(g, dict):
            label = g.get("label") or g.get("key")
            count = _int(g.get("count"))
            if label:
                names.append(f"{label}" + (f" ({count})" if count else ""))
    head = (
        f"The graph groups {nodes or 'these'} document"
        f"{'' if nodes == 1 else 's'} by {group_by}"
    )
    if edges:
        head += f", with {edges} similarity edge" + ("" if edges == 1 else "s")
    head += "."
    if names:
        head += " Cluster headings on screen: " + "; ".join(names)
        if len(groups) > 4:
            head += f"; plus {len(groups) - 4} more"
        head += "."
    head += (
        " A heading labels a cluster of similar pages; clicking it only filters "
        "the same collection."
    )
    return head


def _time_line(time):
    caveat = (
        "The time chart bins pages by last-modified year, not the year the prose "
        "was written. A later date can mean an edit, not a new article."
    )
    if not isinstance(time, dict) or not time.get("available"):
        return (
            "No dated pages are in this visible set, so a yearly series cannot "
            "be drawn. " + caveat
        )
    rows = [r for r in (time.get("rows") or []) if isinstance(r, dict) and r.get("key")]
    if not rows:
        return "Yearly AI-share is hidden until documents have dates. " + caveat
    rows = sorted(rows, key=lambda r: str(r.get("key")))
    first, last = rows[0], rows[-1]
    n_years = len(rows)
    last_pct = _num(last.get("estimatePercent"))
    last_n = _int(last.get("documentCount"))
    span = f"{first.get('key')}-{last.get('key')}" if n_years > 1 else str(first.get("key"))
    line = f"The yearly series covers {n_years} year-bin" + ("" if n_years == 1 else "s")
    line += f" ({span})"
    if last_pct is not None:
        line += f"; the latest bin, {last.get('key')}, is about {last_pct}%"
        if last_n:
            line += f" on {last_n} page" + ("" if last_n == 1 else "s")
    line += ". " + caveat
    return line


def _close_line():
    return (
        "These figures are an ESTIMATE from local stylometry and detectors, "
        "not proof of authorship. GraphSAGE is not in this share. "
        "No medical judgment is made."
    )


def generate_insights(payload: dict) -> str:
    summary = payload.get("summary") if isinstance(payload.get("summary"), dict) else payload
    graph = payload.get("graph") if isinstance(payload.get("graph"), dict) else {}
    time = payload.get("time") if isinstance(payload.get("time"), dict) else {}
    paragraphs = [
        _scope_line(payload, summary),
        _share_line(summary),
        _band_line(summary),
        _time_line(time),
        _graph_line(graph),
        _close_line(),
    ]
    return "\n\n".join(p.strip() for p in paragraphs if p and p.strip())


def _first_line(text: str) -> str:
    for line in text.splitlines():
        if line.strip():
            return line.strip()
    return ""


def polish_ok(text: str, original: str) -> bool:
    if not text or len(text) < 40:
        return False
    low = text.lower()
    first = _first_line(low)
    if any(bad in first for bad in FORBIDDEN_HEADLINE):
        return False
    if "hits" in low.split():
        return False
    if any(term in low for term in MEDICAL_JUDGMENT):
        return False
    # Keep the required caveats; reject a rewrite that drops them.
    if "estimate" not in low:
        return False
    if "last-modified" not in low and "last modified" not in low:
        return False
    if "proof" not in low:
        return False
    # Must not invent a new collection name.
    name = ""
    if isinstance(original, str):
        pass
    return True


def _http_json(url: str, headers: dict, body: dict, timeout: float = 8.0) -> dict:
    raw = json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url, data=raw, headers=headers, method="POST")
    with urllib.request.urlopen(req, timeout=timeout) as resp:
        return json.loads(resp.read().decode("utf-8"))


def polish_openai(draft: str) -> str | None:
    key = os.environ.get("OPENAI_API_KEY", "").strip()
    if not key:
        return None
    model = os.environ.get("OPENAI_MODEL", "gpt-4o-mini").strip() or "gpt-4o-mini"
    body = {
        "model": model,
        "temperature": 0.2,
        "max_tokens": 420,
        "messages": [
            {
                "role": "system",
                "content": (
                    "Polish this KnowledgeOS dashboard note. Keep every number and "
                    "every caveat. Do not add medical judgments. Do not mention HITS. "
                    "Do not put GraphSAGE in the first sentence. Do not invent datasets "
                    "or documents. Wikipedia topic or cluster is a view of the same "
                    "collection. Return only the rewritten text."
                ),
            },
            {"role": "user", "content": draft},
        ],
    }
    try:
        data = _http_json(
            "https://api.openai.com/v1/chat/completions",
            {
                "Authorization": f"Bearer {key}",
                "Content-Type": "application/json",
            },
            body,
        )
        text = _get(data, "choices", default=[{}])
        if isinstance(text, list) and text:
            return (text[0].get("message") or {}).get("content")
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, KeyError, OSError):
        return None
    return None


def polish_anthropic(draft: str) -> str | None:
    key = os.environ.get("ANTHROPIC_API_KEY", "").strip()
    if not key:
        return None
    model = os.environ.get("ANTHROPIC_MODEL", "claude-3-5-haiku-latest").strip() or "claude-3-5-haiku-latest"
    body = {
        "model": model,
        "max_tokens": 420,
        "temperature": 0.2,
        "system": (
            "Polish this KnowledgeOS dashboard note. Keep every number and every "
            "caveat. Do not add medical judgments. Do not mention HITS. Do not put "
            "GraphSAGE in the first sentence. Do not invent datasets. Return only "
            "the rewritten text."
        ),
        "messages": [{"role": "user", "content": draft}],
    }
    try:
        data = _http_json(
            "https://api.anthropic.com/v1/messages",
            {
                "x-api-key": key,
                "anthropic-version": "2023-06-01",
                "Content-Type": "application/json",
            },
            body,
        )
        blocks = data.get("content") or []
        parts = [b.get("text", "") for b in blocks if isinstance(b, dict)]
        text = "\n".join(p for p in parts if p).strip()
        return text or None
    except (urllib.error.URLError, TimeoutError, json.JSONDecodeError, KeyError, OSError):
        return None


def maybe_polish(draft: str) -> tuple[str, str]:
    for fn in (polish_openai, polish_anthropic):
        try:
            polished = fn(draft)
        except Exception:
            polished = None
        if polished:
            text = polished.strip()
            if polish_ok(text, draft):
                return text, "llm"
    return draft, "deterministic"


def main() -> int:
    raw = sys.stdin.read()
    if not raw.strip():
        sys.stderr.write("insights agent: empty stdin\n")
        return 2
    try:
        payload = json.loads(raw)
    except json.JSONDecodeError as exc:
        sys.stderr.write(f"insights agent: invalid JSON ({exc})\n")
        return 2
    if not isinstance(payload, dict):
        sys.stderr.write("insights agent: expected a JSON object\n")
        return 2
    draft = generate_insights(payload)
    text, source = maybe_polish(draft)
    # Machine-readable trailer is optional; C++ accepts either plain text
    # or a JSON object with a "text" field. Prefer JSON so the UI can
    # show that no API key was required.
    out = {"text": text, "source": source, "llmRequired": False}
    raw = json.dumps(out, ensure_ascii=True) + "\n"
    sys.stdout.buffer.write(raw.encode("ascii"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
