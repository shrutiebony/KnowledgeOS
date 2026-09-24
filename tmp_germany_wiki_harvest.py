"""Harvest unused German place articles into Wikipedia dataset 1. India/US/Australia untouched."""

from __future__ import annotations

import json
import sqlite3
import time
import urllib.error
import urllib.parse
import urllib.request

UA = "KnowledgeOS-Wiki/1.0 (local educational corpus ingest; en.wikipedia.org general collection)"
API = "https://en.wikipedia.org/w/api.php"
DB = "data/knowledgeos.db"
DATASET_ID = 1
TARGET = 500
MIN_WORDS = 60
SOURCE = "wikipedia.org"
TOPIC = "Germany"

CATS = [
    "Category:Towns in Bavaria",
    "Category:Towns in Baden-Württemberg",
    "Category:Towns in North Rhine-Westphalia",
    "Category:Towns in Lower Saxony",
    "Category:Towns in Hesse",
    "Category:Towns in Rhineland-Palatinate",
    "Category:Towns in Saxony",
    "Category:Towns in Thuringia",
    "Category:Towns in Schleswig-Holstein",
    "Category:Towns in Saxony-Anhalt",
    "Category:Towns in Mecklenburg-Vorpommern",
    "Category:Towns in Brandenburg",
    "Category:Towns in Saarland",
    "Category:Towns in Germany",
    "Category:Cities in Saxony",
    "Category:Cities in Thuringia",
    "Category:Cities in Schleswig-Holstein",
    "Category:Cities in Rhineland-Palatinate",
    "Category:Cities in Saarland",
    "Category:Cities in Mecklenburg-Vorpommern",
    "Category:Cities in Saxony-Anhalt",
    "Category:Cities in Brandenburg",
    "Category:Municipalities in Bavaria",
    "Category:Municipalities in Baden-Württemberg",
    "Category:Municipalities in North Rhine-Westphalia",
    "Category:Municipalities in Lower Saxony",
    "Category:Municipalities in Hesse",
    "Category:Municipalities in Rhineland-Palatinate",
    "Category:Municipalities in Saxony",
    "Category:Municipalities in Thuringia",
]

SKIP_NS = {
    "special", "file", "image", "talk", "user", "wikipedia", "wp", "help", "template",
    "module", "mediawiki", "draft", "category", "portal",
}


def wiki_url(title: str) -> str:
    return "https://en.wikipedia.org/wiki/" + urllib.parse.quote(title.replace(" ", "_"), safe=":_()%,.-")


def skip_title(title: str) -> bool:
    low = title.lower()
    if "(disambiguation)" in low:
        return True
    if ":" in title:
        ns = title.split(":", 1)[0].lower().replace(" ", "_")
        if ns in SKIP_NS or ns.endswith("_talk"):
            return True
    return False


def looks_disambiguation(title: str, text: str) -> bool:
    if "(disambiguation)" in title.lower():
        return True
    head = text[:180].lower()
    return "may refer to" in head and len(text.split()) < 120


def api_get(params: dict) -> dict:
    q = dict(params)
    q.setdefault("format", "json")
    q.setdefault("formatversion", "2")
    url = API + "?" + urllib.parse.urlencode(q)
    wait = 4
    last_err = None
    for attempt in range(12):
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        try:
            with urllib.request.urlopen(req, timeout=45) as resp:
                body = resp.read().decode("utf-8", "replace")
                return json.loads(body)
        except urllib.error.HTTPError as e:
            last_err = e
            if e.code in (429, 503):
                print(f"Wikipedia HTTP {e.code}, backoff {wait}s", flush=True)
                time.sleep(wait)
                wait = min(wait * 2, 60)
                continue
            raise
        except Exception as e:
            last_err = e
            print(f"Wikipedia request failed ({e}), retry in {wait}s", flush=True)
            time.sleep(wait)
            wait = min(wait * 2, 60)
    raise RuntimeError(f"Wikipedia API failed after retries: {last_err}")


def category_members(cat: str) -> list[str]:
    titles: list[str] = []
    cont = None
    for _ in range(8):
        params = {
            "action": "query",
            "list": "categorymembers",
            "cmtype": "page",
            "cmnamespace": "0",
            "cmlimit": "500",
            "cmtitle": cat,
        }
        if cont:
            params["cmcontinue"] = cont
        data = api_get(params)
        members = data.get("query", {}).get("categorymembers", [])
        for m in members:
            t = m.get("title") or ""
            if t and not skip_title(t):
                titles.append(t)
        cont = (data.get("continue") or {}).get("cmcontinue")
        if not cont:
            break
        time.sleep(1.5)
    print(f"queued {len(titles)} titles from {cat}", flush=True)
    return titles


def extracts(titles: list[str]) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    joined = "|".join(titles)
    data = api_get({
        "action": "query",
        "prop": "extracts",
        "explaintext": "1",
        "exintro": "1",
        "exsectionformat": "plain",
        "redirects": "1",
        "exchars": "1800",
        "exlimit": "20",
        "titles": joined,
    })
    aliases: dict[str, str] = {}
    query = data.get("query") or {}
    for n in query.get("normalized") or []:
        if n.get("from") and n.get("to"):
            aliases[n["to"]] = n["from"]
    for r in query.get("redirects") or []:
        if r.get("from") and r.get("to"):
            aliases[r["to"]] = aliases.get(r["from"], r["from"])
    for p in query.get("pages") or []:
        title = p.get("title") or ""
        text = p.get("extract") or ""
        if title and text:
            out.append((title, text))
    return out


def open_db(retries: int = 20) -> sqlite3.Connection:
    last = None
    for i in range(retries):
        try:
            con = sqlite3.connect(DB, timeout=30)
            con.execute("PRAGMA busy_timeout=30000")
            con.execute("SELECT COUNT(*) FROM documents").fetchone()
            return con
        except sqlite3.Error as e:
            last = e
            print(f"DB open failed ({e}), retry {i + 1}", flush=True)
            time.sleep(2)
    raise RuntimeError(f"Could not open DB: {last}")


def existing_urls(con: sqlite3.Connection) -> set[str]:
    rows = con.execute("SELECT url FROM documents WHERE dataset_id=?", (DATASET_ID,)).fetchall()
    return {r[0] for r in rows if r[0]}


def germany_count(con: sqlite3.Connection) -> int:
    return int(con.execute(
        "SELECT COUNT(*) FROM documents WHERE dataset_id=? AND topic=?",
        (DATASET_ID, TOPIC),
    ).fetchone()[0])


def insert_page(con: sqlite3.Connection, title: str, text: str, url: str) -> None:
    words = len(text.split())
    con.execute(
        "INSERT INTO documents(dataset_id,title,url,source,topic,published_at,created_at,text,word_count) "
        "VALUES(?,?,?,?,?,?,?,?,?)",
        (DATASET_ID, title, url, SOURCE, TOPIC, None, None, text, words),
    )
    con.commit()


def main() -> None:
    con = open_db()
    have = existing_urls(con)
    n_de = germany_count(con)
    print(f"start Germany={n_de} wiki_urls={len(have)} target={TARGET}", flush=True)
    if n_de >= TARGET:
        print("already at target", flush=True)
        return

    seen_titles: set[str] = set()
    stored = 0
    for cat in CATS:
        if n_de + stored >= TARGET:
            break
        titles = [t for t in category_members(cat) if t not in seen_titles]
        for t in titles:
            seen_titles.add(t)
        batch: list[str] = []
        for title in titles:
            if n_de + stored >= TARGET:
                break
            url = wiki_url(title)
            if url in have:
                continue
            batch.append(title)
            if len(batch) < 20:
                continue
            time.sleep(1.6)
            for t, text in extracts(batch):
                if n_de + stored >= TARGET:
                    break
                if skip_title(t) or looks_disambiguation(t, text):
                    continue
                if len(text.split()) < MIN_WORDS:
                    continue
                u = wiki_url(t)
                if u in have:
                    continue
                insert_page(con, t, text, u)
                have.add(u)
                stored += 1
                if stored <= 5 or stored % 25 == 0:
                    print(f"stored +{stored} Germany now {n_de + stored} ({t})", flush=True)
            batch = []
        if batch and n_de + stored < TARGET:
            time.sleep(1.6)
            for t, text in extracts(batch):
                if n_de + stored >= TARGET:
                    break
                if skip_title(t) or looks_disambiguation(t, text):
                    continue
                if len(text.split()) < MIN_WORDS:
                    continue
                u = wiki_url(t)
                if u in have:
                    continue
                insert_page(con, t, text, u)
                have.add(u)
                stored += 1
                if stored <= 5 or stored % 25 == 0:
                    print(f"stored +{stored} Germany now {n_de + stored} ({t})", flush=True)

    final = germany_count(con)
    print(f"done new={stored} Germany={final}", flush=True)
    con.close()


if __name__ == "__main__":
    main()
