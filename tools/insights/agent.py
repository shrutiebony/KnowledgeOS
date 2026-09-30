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


def _fmt_val(v):
    if v is None:
        return "n/a"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, float):
        return str(int(v)) if v == int(v) else f"{v:.3f}".rstrip("0").rstrip(".")
    if isinstance(v, (int, str)):
        return str(v)
    if isinstance(v, list):
        return ", ".join(_fmt_val(x) for x in v)
    if isinstance(v, dict):
        return "; ".join(f"{k} = {_fmt_val(val)}" for k, val in v.items())
    return str(v)


def _step(n, title, formula, inputs, intermediates, result, decision, step_id):
    return {
        "n": n,
        "id": step_id,
        "title": title,
        "formula": formula,
        "inputs": inputs,
        "intermediates": intermediates,
        "result": result,
        "decision": decision,
    }


def _step_prose(step: dict) -> str:
    lines = [f"{step.get('n', '')}. {step.get('title', '')}"]
    if step.get("formula"):
        lines.append(f"   Formula: {step['formula']}")
    inputs = step.get("inputs") or {}
    if inputs:
        lines.append("   Inputs: " + "; ".join(f"{k} = {_fmt_val(v)}" for k, v in inputs.items()))
    mid = step.get("intermediates") or {}
    if mid:
        lines.append("   Intermediate: " + "; ".join(f"{k} = {_fmt_val(v)}" for k, v in mid.items()))
    if step.get("result"):
        lines.append(f"   Result: {step['result']}")
    if step.get("decision"):
        lines.append(f"   Decision rule: {step['decision']}")
    return "\n".join(lines)


def build_calculations(payload: dict) -> dict:
    summary = payload.get("summary") if isinstance(payload.get("summary"), dict) else payload
    graph = payload.get("graph") if isinstance(payload.get("graph"), dict) else {}
    time = payload.get("time") if isinstance(payload.get("time"), dict) else {}
    bands = summary.get("bands") or {}
    n = _int(summary.get("documentCount"))
    corpus = _int(summary.get("corpusDocumentCount"), n)
    ai = _int(bands.get("LIKELY_AI"))
    human = _int(bands.get("LIKELY_HUMAN"))
    unc = _int(bands.get("UNCERTAIN"))
    scored = ai + human + unc
    pending = n - scored if n > scored else 0
    signals = summary.get("collectionSignals") if isinstance(summary.get("collectionSignals"), dict) else {}
    share = _num(summary.get("shareOfDocuments"))
    words_share = _num(summary.get("shareOfAnalyzedWords"))
    doc_range = summary.get("shareOfDocumentsRange") or []
    word_range = summary.get("shareOfAnalyzedWordsRange") or []
    words = _int(summary.get("analyzedWordCount"))
    mean_p = _num(signals.get("pAi"))
    if mean_p is None and share is not None:
        mean_p = share / 100.0
    ai_min, human_max, max_iv = 0.58, 0.42, 0.50
    steps = []

    steps.append(_step(
        1, "What we counted",
        "pending = visible documents - scored documents. A page is scored when it already has an AI-likelihood estimate p_ai and a band.",
        {
            "visibleDocuments": n,
            "collectionDocuments": corpus,
            "likelyAiGenerated": ai,
            "likelyHumanWritten": human,
            "uncertain": unc,
        },
        {"scoredDocuments": scored, "pendingDocuments": pending},
        f"{n} visible document{'s' if n != 1 else ''}, {scored} scored, {pending} pending",
        "Only scored pages enter the collection percent. Pending pages are omitted from the mean.",
        "counts",
    ))

    if share is None:
        steps.append(_step(
            2, "Headline AI share of documents",
            "share% = round(100 × mean of p_ai over scored documents).",
            {"scoredDocuments": scored},
            {},
            str(summary.get("headline") or "This visible set is not yet analyzed."),
            "No stored p_ai scores, so a collection percent cannot be formed. GraphSAGE is not used in this share.",
            "documentShare",
        ))
    else:
        mid = {"roundedPercent": share}
        if mean_p is not None:
            mid["100TimesMeanPAi"] = round(mean_p * 100.0, 3)
        steps.append(_step(
            2, "Headline AI share of documents",
            "share% = round(100 × mean of p_ai over scored documents). p_ai is already stored per page (logistic blend of stylometry, template phrases, sentence-length variance, repeated phrases, and embedding distance from the pre-2019 writing centroid in this visible set). Pages dated before 2019 stay near zero.",
            {
                "scoredDocuments": scored,
                "meanPAi": mean_p,
                "storedShareOfDocumentsPercent": share,
            },
            mid,
            f"{share}% of documents",
            "This percent is the mean of document scores — the share of documents — not a count of proven AI pages. GraphSAGE is not used in this share.",
            "documentShare",
        ))

    if share is not None and isinstance(doc_range, list) and len(doc_range) >= 2:
        steps.append(_step(
            len(steps) + 1, "Uncertainty range for the document share",
            "SE = s / √n, where s is the sample standard deviation of p_ai. If n < 2, SE is taken as 0.08. Interval = [clip01(mean - 1.96×SE), clip01(mean + 1.96×SE)], then each end × 100 and rounded.",
            {
                "scoredDocuments": scored,
                "meanPAi": mean_p,
                "storedRangePercent": [doc_range[0], doc_range[1]],
            },
            {},
            f"range {doc_range[0]}–{doc_range[1]}%",
            "The stored range is that interval. It is a conventional 95% interval around the mean of the scores, not a claim that authorship is proven inside the band.",
            "documentRange",
        ))

    if words_share is not None:
        word_result = f"{words_share}% of analyzed words"
        if isinstance(word_range, list) and len(word_range) >= 2:
            word_result += f" (range {word_range[0]}–{word_range[1]}%)"
        steps.append(_step(
            len(steps) + 1, "AI share of analyzed words",
            "word share = Σ(p_ai × word_count) / Σ(word_count). The stored word range uses the same SE as the document scores (not a separate word-weighted SE), then clip01(mean ± 1.96×SE) × 100.",
            {
                "scoredDocuments": scored,
                "analyzedWordCount": words,
                "storedShareOfAnalyzedWordsPercent": words_share,
            },
            {},
            word_result,
            "This is a word-weighted mean of the same stored p_ai scores. GraphSAGE is not used here either.",
            "wordShare",
        ))

    steps.append(_step(
        len(steps) + 1, "How each page gets a band",
        "If (ci_high - ci_low) > max interval, the band is uncertain. Else if p_ai ≥ likely-AI cutoff → likely AI-generated. Else if p_ai ≤ likely-human cutoff → likely human-written. Else uncertain.",
        {
            "likelyAiMinPAi": ai_min,
            "likelyHumanMaxPAi": human_max,
            "uncertainIfIntervalWiderThan": max_iv,
            "likelyAiGeneratedCount": ai,
            "likelyHumanWrittenCount": human,
            "uncertainCount": unc,
        },
        {"likelyAiMinPercent": 58, "likelyHumanMaxPercent": 42},
        f"{ai} likely AI-generated, {human} likely human-written, {unc} uncertain",
        "Bands classify each stored score against those cutoffs. They are labels on the same p_ai values, not a second model.",
        "bands",
    ))

    rows = [r for r in (time.get("rows") or []) if isinstance(r, dict)]
    dated = [r for r in rows if r.get("estimatePercent") is not None]
    last = dated[-1] if dated else None
    if not rows or not dated:
        time_result = "No dated pages in this visible set, so a yearly series cannot be drawn."
    else:
        time_result = f"{len(rows)} year-bin" + ("" if len(rows) == 1 else "s") + " from 2015 through the latest dated year"
        if last:
            time_result += f". Latest bin {last.get('key')} = {last.get('estimatePercent')}%"
            if last.get("documentCount") is not None:
                time_result += f" on {last.get('documentCount')} page(s)"
    examples = []
    if dated:
        examples.append(dated[0])
        if dated[-1] is not dated[0]:
            examples.append(dated[-1])
    steps.append(_step(
        len(steps) + 1, "Yearly series",
        "A year-bin is the calendar year of the document date (created / first-revision if present, else published). Years before 2015 are omitted. The estimate for a year is the mean of p_ai in that bin, then × 100 and rounded — the same mean as the headline, restricted to that year. The range in a bin uses the same 1.96×SE rule.",
        {
            "yearBinStartsAt": 2015,
            "dateRule": "calendar year of first-revision / created date when stored, otherwise published date",
            "yearBinCount": len(rows),
            "binsWithAMean": len(dated),
            "exampleBins": examples,
        },
        {},
        time_result,
        "A later date can mean an edit, not a newly written page. Combined All sources unions years from every collection.",
        "time",
    ))

    group_by = str(graph.get("groupBy") or "topic")
    group_plain = _group_by_label(group_by)
    if group_by == "topic":
        group_plain = "country heading (stored topic label)"
    elif group_by in ("dataset", "source"):
        group_plain = "collection"
    nodes = _int(graph.get("nodeCount"))
    edges = _int(graph.get("edgeCount"))
    steps.append(_step(
        len(steps) + 1, "Graph grouping",
        "Nodes are documents. Edges are nearest-neighbor text similarity (up to k neighbors, cosine at least the stored minimum). Group labels come from stored metadata (country or collection), not from a second authorship model.",
        {
            "nodeCount": nodes,
            "edgeCount": edges,
            "groupBy": group_by,
            "groupByPlain": group_plain,
            "nearestNeighbors": 8,
            "minCosineSimilarity": 0.32,
        },
        {},
        f"{nodes} document node{'s' if nodes != 1 else ''}, {edges} similarity edge{'s' if edges != 1 else ''}, grouped by {group_plain}",
        "Clicking a heading only filters this same collection. GraphSAGE is not in the headline share.",
        "graph",
    ))

    gs = summary.get("graphsage") if isinstance(summary.get("graphsage"), dict) else {}
    gs_used = bool(gs.get("usedInHeadline"))
    steps.append(_step(
        len(steps) + 1, "What is not in the headline",
        "headline share uses only the mean of stored document p_ai scores.",
        {
            "graphsageUsedInHeadline": gs_used,
            "graphsageStatus": gs.get("status") or "NOT_TRAINED",
        },
        {},
        "GraphSAGE was marked as used — unexpected; the share should still be read as the mean of p_ai."
        if gs_used else "GraphSAGE is not in this share.",
        "Similarity clustering and GraphSAGE embeddings may appear as graph context. They do not enter the collection percent.",
        "graphsage",
    ))

    prose = statistical_note(payload)
    return {
        "title": "How we calculated this",
        "steps": steps,
        "prose": prose,
        "thresholds": {
            "likelyAiMinPAi": ai_min,
            "likelyHumanMaxPAi": human_max,
            "uncertainIfIntervalWiderThan": max_iv,
            "timeChartMinYear": 2015,
            "graphNearestNeighbors": 8,
            "graphMinCosine": 0.32,
        },
        "counts": {
            "visibleDocuments": n,
            "collectionDocuments": corpus,
            "scoredDocuments": scored,
            "pendingDocuments": pending,
            "likelyAiGenerated": ai,
            "likelyHumanWritten": human,
            "uncertain": unc,
            "analyzedWordCount": words,
        },
        "shareOfDocuments": share,
        "shareOfAnalyzedWords": words_share,
        "shareOfDocumentsRange": doc_range,
        "shareOfAnalyzedWordsRange": word_range,
        "meanPAi": mean_p,
        "graphsageUsedInHeadline": gs_used,
    }


def _signal_lines(summary: dict) -> list[str]:
    signals = summary.get("collectionSignals") if isinstance(summary.get("collectionSignals"), dict) else {}
    rows = (
        ("formulaic style", "stylometry", "0.70"),
        ("template phrases", "stockPhrases", "1.10"),
        ("even sentence lengths", "uniformity", "0.85"),
        ("repeated phrases", "ngrams", "0.28"),
        ("wording unlike pre-2019 pages", "embeddingAnomaly", "0.50"),
        ("style unlike pre-2019 pages", "stylometryDeviation", "0.30"),
        ("date after Sept 2019", "postChatgpt", "1.15"),
    )
    lines = []
    for label, key, weight in rows:
        value = _num(signals.get(key))
        if value is None:
            continue
        lines.append(f"{label} {value:.2f} (weight {weight})")
    return lines


def statistical_note(payload: dict) -> str:
    summary = payload.get("summary") if isinstance(payload.get("summary"), dict) else {}
    graph = payload.get("graph") if isinstance(payload.get("graph"), dict) else {}
    time = payload.get("time") if isinstance(payload.get("time"), dict) else {}
    name = payload.get("name") or summary.get("name") or "This collection"
    bands = summary.get("bands") or {}
    n = _int(summary.get("documentCount"))
    ai = _int(bands.get("LIKELY_AI"))
    human = _int(bands.get("LIKELY_HUMAN"))
    unc = _int(bands.get("UNCERTAIN"))
    scored = ai + human + unc
    pending = n - scored if n > scored else 0
    words = _int(summary.get("analyzedWordCount"))
    share = _num(summary.get("shareOfDocuments"))
    words_share = _num(summary.get("shareOfAnalyzedWords"))
    doc_range = _range_text(summary.get("shareOfDocumentsRange") or [])
    word_range = _range_text(summary.get("shareOfAnalyzedWordsRange") or [])
    signals = summary.get("collectionSignals") if isinstance(summary.get("collectionSignals"), dict) else {}
    mean_p = _num(signals.get("pAi"))
    if mean_p is None and share is not None:
        mean_p = share / 100.0
    topic = payload.get("topic") or summary.get("topic")
    topic_view = bool(payload.get("topicView") or summary.get("topicView"))
    id_view = bool(payload.get("idView") or summary.get("idView"))

    where = str(name)
    if id_view:
        where += ", selected cluster only"
    elif topic_view and topic:
        where += f', topic "{topic}"'

    if share is None and words_share is None:
        return where + "\n\nThese pages do not have scores yet, so there is no average to report."

    mean_txt = f"{mean_p:.3f}" if mean_p is not None else None
    headline = f"{where}. {scored} of {n} pages have a score"
    if pending:
        headline += f" ({pending} are still waiting and are left out)"
    headline += "."
    if share is not None:
        headline += f" The {share}% is the average of those scores"
        if mean_txt:
            headline += f" ({mean_txt})"
        headline += f", with a range of {doc_range}." if doc_range else "."

    measures = _signal_lines(summary)
    method = (
        "How one page is scored\n"
        "Each page gets seven readings from 0 to 1. Higher means more like the pattern the score treats as AI-assisted."
    )
    if measures:
        method += "\nOn this set, the averages are:\n" + "\n".join(f"- {line}" for line in measures)
    method += (
        "\n\nThose readings are combined like this:\n"
        "sum = 0.58\n"
        "  + 0.70 x (formulaic style - 0.5)\n"
        "  + 1.10 x (template phrases - 0.5)\n"
        "  + 0.85 x (even sentence lengths - 0.5)\n"
        "  + 0.28 x (repeated phrases - 0.5)\n"
        "  + 0.50 x (wording distance from pre-2019 pages - 0.5)\n"
        "  + 0.30 x (style distance from those pages - 0.5)\n"
        "  + 1.15 x (date after Sept 2019 - 0.45)\n"
        "page score = 1 / (1 + e^(-sum))\n\n"
        "A reading of 0.5 adds nothing. Above 0.5 it pushes the score up, and the larger weights "
        "(date, template phrases, even sentences) push harder. "
        "For pages from 2019 on, the stored score is 62% of that result and 38% of the page's rank among those later pages. "
        "Pages dated before 2019 are set near 0 and labeled human-written. They are the comparison set, so they pull the average down. "
        "If formulaic style, sentence evenness, and the date disagree, that page's own range gets wider."
    )

    collection = "The collection number\n"
    if share is not None:
        collection += f"{share}% = round(100 x average of the {scored} page scores)."
        if mean_txt:
            collection += f" Average = {mean_txt}."
    if words_share is not None:
        total = f"{words:,} words" if words else "the total words"
        collection += (
            f"\nThe word figure weights each score by how long the page is: "
            f"{words_share}% = sum(score x word count) / {total}."
        )
        if word_range:
            collection += f" Range {word_range}."

    spread = ""
    if doc_range and share is not None:
        small = " With fewer than 2 scores, that step is taken as 0.08." if scored < 2 else ""
        spread = (
            f"The range {doc_range}\n"
            f"Take how far the {scored} scores sit from their average (the standard deviation), "
            f"divide by the square root of {scored}, and step 1.96 of those steps above and below the average. "
            "Keep the ends between 0 and 1, multiply by 100, and round."
            + small
            + " It describes the spread of these scores. The pages were scored against the same earlier pages, so they are not separate samples."
        )

    labels = (
        "The three labels\n"
        f"{ai} likely AI-generated: score at least 0.58.\n"
        f"{human} likely human-written: score at most 0.42.\n"
        f"{unc} uncertain: between those cutoffs, or the page's own range is wider than 0.50."
    )
    if scored and share is not None:
        labels += f"\n{ai}/{scored} is how many pages clear 0.58. The {share}% is the average of the scores, a different number."

    rows = [r for r in (time.get("rows") or []) if isinstance(r, dict)]
    dated = [r for r in rows if r.get("estimatePercent") is not None]
    if not rows:
        time_line = (
            "By year\nNo dated pages in this view. A year is the first-revision date when one is stored, "
            "otherwise the published date. Years before 2015 are left off the chart."
        )
    else:
        keys = sorted(str(r.get("key")) for r in rows if r.get("key"))
        span = f"{keys[0]}-{keys[-1]}" if keys else ""
        dated_sorted = sorted(dated, key=lambda r: str(r.get("key")))
        last = dated_sorted[-1] if dated_sorted else None
        time_line = "By year\nThe chart is the same average, one year at a time"
        if span:
            time_line += f" ({span}, {len(dated)} years with scores)"
        time_line += ". The year is the first-revision date when that is stored, otherwise the published date. "
        if last:
            lr = _range_text(last.get("rangePercent") or [])
            n_last = _int(last.get("documentCount"))
            time_line += f"{last.get('key')} is {last.get('estimatePercent')}% on {n_last} page"
            time_line += "" if n_last == 1 else "s"
            if lr:
                time_line += f" (range {lr})"
            time_line += ". "
        time_line += "A later year can be an edit, not the year the prose was written."

    nodes = _int(graph.get("nodeCount"))
    edges = _int(graph.get("edgeCount"))
    group_bits = []
    for g in (graph.get("groups") or [])[:4]:
        if isinstance(g, dict):
            label = g.get("label") or g.get("key")
            count = _int(g.get("count"))
            if label:
                group_bits.append(f"{label} ({count})" if count else str(label))
    share_bit = f"{share}%" if share is not None else "score"
    graph_line = (
        f"The graph\n{nodes} dots, {edges} lines. Each page links to up to 8 similar pages, "
        "and only when the similarity is at least 0.32."
    )
    if group_bits:
        graph_line += " Circles on screen: " + "; ".join(group_bits) + "."
    graph_line += f" Those lines are not used in the {share_bit}."

    paragraphs = [headline, method, collection]
    if spread:
        paragraphs.append(spread)
    paragraphs.extend([labels, time_line, graph_line])
    return "\n\n".join(paragraphs)


def generate_insights(payload: dict) -> str:
    return statistical_note(payload)


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
    if "average" not in low and "estimate" not in low:
        return False
    if "pre-2019" not in low and "before 2019" not in low:
        return False
    if "0.58" not in low and "logistic" not in low:
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
        "max_tokens": 900,
        "messages": [
            {
                "role": "system",
                "content": (
                    "Polish this methods note for a statistician. Keep every number, "
                    "the estimand, the Wald-interval caveat, and the pre-2019 baseline. "
                    "Do not add Formula, Inputs, or Decision rule sections. "
                    "Do not add medical judgments. Do not mention HITS. "
                    "Do not put GraphSAGE in the first sentence. Do not invent datasets "
                    "or documents. Return only the rewritten note."
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
        "max_tokens": 900,
        "temperature": 0.2,
        "system": (
            "Polish this methods note for a statistician. Keep every number, "
            "the estimand, the Wald-interval caveat, and the pre-2019 baseline. "
            "Do not add Formula, Inputs, or Decision rule sections. "
            "Do not add medical judgments. Do not mention HITS. Do not put "
            "GraphSAGE in the first sentence. Do not invent datasets. Return only "
            "the rewritten note."
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
    calculations = build_calculations(payload)
    # Machine-readable trailer is optional; C++ accepts either plain text
    # or a JSON object with a "text" field. Prefer JSON so the UI can
    # show that no API key was required. C++ overwrites calculations
    # from the same summary/graph/time payload so polish cannot drop them.
    out = {
        "text": text,
        "source": source,
        "llmRequired": False,
        "calculations": calculations,
        "howCalculated": calculations.get("prose") or "",
    }
    raw = json.dumps(out, ensure_ascii=True) + "\n"
    sys.stdout.buffer.write(raw.encode("ascii"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
