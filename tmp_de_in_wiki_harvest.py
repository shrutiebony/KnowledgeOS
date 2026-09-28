"""Fill Wikipedia Germany and India toward 1000 pages. Australia is not harvested."""

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
TARGET = 1000
MIN_WORDS = 60
SOURCE = "wikipedia.org"

CAMPAIGNS = [
    (
        "Germany",
        [
            "Category:Towns in Rhineland-Palatinate",
            "Category:Towns in Saxony",
            "Category:Towns in Thuringia",
            "Category:Towns in Schleswig-Holstein",
            "Category:Towns in Saxony-Anhalt",
            "Category:Towns in Mecklenburg-Vorpommern",
            "Category:Towns in Brandenburg",
            "Category:Towns in Saarland",
            "Category:Municipalities in Bavaria",
            "Category:Municipalities in Baden-Württemberg",
            "Category:Municipalities in North Rhine-Westphalia",
            "Category:Municipalities in Lower Saxony",
            "Category:Municipalities in Hesse",
            "Category:Cities in Saxony",
            "Category:Cities in Brandenburg",
        ],
    ),
    (
        "India",
        [
            "Category:Cities in Maharashtra",
            "Category:Cities in Uttar Pradesh",
            "Category:Cities in Tamil Nadu",
            "Category:Cities in Karnataka",
            "Category:Cities in West Bengal",
            "Category:Cities in Gujarat",
            "Category:Cities in Rajasthan",
            "Category:Cities in Kerala",
            "Category:Cities in Madhya Pradesh",
            "Category:Cities in India",
            "Category:Populated places in India",
        ],
    ),
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
    for _ in range(12):
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        try:
            with urllib.request.urlopen(req, timeout=45) as resp:
                return json.loads(resp.read().decode("utf-8", "replace"))
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
        for m in data.get("query", {}).get("categorymembers", []):
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
    data = api_get({
        "action": "query",
        "prop": "extracts",
        "explaintext": "1",
        "exintro": "1",
        "exsectionformat": "plain",
        "redirects": "1",
        "exchars": "1800",
        "exlimit": "20",
        "titles": "|".join(titles),
    })
    out = []
    for p in (data.get("query") or {}).get("pages") or []:
        title = p.get("title") or ""
        text = p.get("extract") or ""
        if title and text:
            out.append((title, text))
    return out


def open_db() -> sqlite3.Connection:
    last = None
    for i in range(20):
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


def topic_count(con: sqlite3.Connection, topic: str) -> int:
    if topic == "India":
        return int(con.execute(
            "SELECT COUNT(*) FROM documents WHERE dataset_id=? AND topic IN ('India','Geography of India')",
            (DATASET_ID,),
        ).fetchone()[0])
    return int(con.execute(
        "SELECT COUNT(*) FROM documents WHERE dataset_id=? AND topic=?",
        (DATASET_ID, topic),
    ).fetchone()[0])


def main() -> None:
    con = open_db()
    have = {r[0] for r in con.execute("SELECT url FROM documents WHERE dataset_id=?", (DATASET_ID,)).fetchall() if r[0]}
    print(
        f"start Germany={topic_count(con, 'Germany')} India={topic_count(con, 'India')} "
        f"Australia={topic_count(con, 'Australia')} urls={len(have)}",
        flush=True,
    )
    seen: set[str] = set()
    for topic, cats in CAMPAIGNS:
        have_n = topic_count(con, topic)
        stored = 0
        print(f"harvest {topic} have {have_n} want {TARGET}", flush=True)
        if have_n >= TARGET:
            continue
        for cat in cats:
            if have_n + stored >= TARGET:
                break
            titles = [t for t in category_members(cat) if t not in seen]
            for t in titles:
                seen.add(t)
            batch: list[str] = []
            for title in titles:
                if have_n + stored >= TARGET:
                    break
                if wiki_url(title) in have:
                    continue
                batch.append(title)
                if len(batch) < 20:
                    continue
                time.sleep(1.6)
                for t, text in extracts(batch):
                    if have_n + stored >= TARGET:
                        break
                    if skip_title(t) or looks_disambiguation(t, text) or len(text.split()) < MIN_WORDS:
                        continue
                    u = wiki_url(t)
                    if u in have:
                        continue
                    con.execute(
                        "INSERT INTO documents(dataset_id,title,url,source,topic,published_at,created_at,text,word_count) "
                        "VALUES(?,?,?,?,?,?,?,?,?)",
                        (DATASET_ID, t, u, SOURCE, topic, None, None, text, len(text.split())),
                    )
                    con.commit()
                    have.add(u)
                    stored += 1
                    if stored <= 5 or stored % 25 == 0:
                        print(f"stored +{stored} {topic} now {have_n + stored} ({t})", flush=True)
                batch = []
            if batch and have_n + stored < TARGET:
                time.sleep(1.6)
                for t, text in extracts(batch):
                    if have_n + stored >= TARGET:
                        break
                    if skip_title(t) or looks_disambiguation(t, text) or len(text.split()) < MIN_WORDS:
                        continue
                    u = wiki_url(t)
                    if u in have:
                        continue
                    con.execute(
                        "INSERT INTO documents(dataset_id,title,url,source,topic,published_at,created_at,text,word_count) "
                        "VALUES(?,?,?,?,?,?,?,?,?)",
                        (DATASET_ID, t, u, SOURCE, topic, None, None, text, len(text.split())),
                    )
                    con.commit()
                    have.add(u)
                    stored += 1
                    if stored <= 5 or stored % 25 == 0:
                        print(f"stored +{stored} {topic} now {have_n + stored} ({t})", flush=True)
        print(f"done {topic} now {topic_count(con, topic)} new={stored}", flush=True)
    print(
        f"final Germany={topic_count(con, 'Germany')} India={topic_count(con, 'India')} "
        f"Australia={topic_count(con, 'Australia')}",
        flush=True,
    )
    con.close()


if __name__ == "__main__":
    main()
