import json
import sqlite3
import time
import urllib.error
import urllib.parse
import urllib.request

UA = "KnowledgeOS-WikiIndia/1.0 (local educational corpus ingest; en.wikipedia.org India collection)"
DB = r"C:\Users\sgoya\Downloads\KnowledgeOS\KnowledgeOS\data\knowledgeos.db"
API = "https://en.wikipedia.org/w/api.php"


def title_key(url, title):
    if url:
        path = urllib.parse.urlparse(url).path
        if path.startswith("/wiki/"):
            t = urllib.parse.unquote(path[6:]).replace("_", " ").strip()
            if t:
                return t
    return (title or "").replace("_", " ").strip()


def first_rev(title):
    q = urllib.parse.urlencode(
        {
            "action": "query",
            "format": "json",
            "formatversion": "2",
            "prop": "revisions",
            "rvprop": "timestamp",
            "redirects": "1",
            "rvdir": "newer",
            "rvlimit": "1",
            "titles": title,
        }
    )
    req = urllib.request.Request(API + "?" + q, headers={"User-Agent": UA, "Accept": "application/json"})
    delay = 15
    for attempt in range(3):
        try:
            with urllib.request.urlopen(req, timeout=30) as resp:
                body = json.load(resp)
        except urllib.error.HTTPError as e:
            print("http", e.code, title, "sleep", delay, flush=True)
            time.sleep(delay)
            delay = min(delay * 2, 60)
            continue
        except Exception as e:
            print("net", type(e).__name__, e, title, flush=True)
            time.sleep(delay)
            delay = min(delay * 2, 60)
            continue
        if "error" in body:
            print("api", body["error"].get("code"), title, flush=True)
            time.sleep(delay)
            delay = min(delay * 2, 60)
            continue
        pages = body.get("query", {}).get("pages", [])
        if not pages:
            return None
        revs = pages[0].get("revisions") or []
        if not revs:
            return None
        ts = revs[0].get("timestamp", "")
        return ts[:10] if len(ts) >= 10 else None
    return None


def main():
    conn = sqlite3.connect(DB, timeout=60, isolation_level=None)
    conn.execute("PRAGMA busy_timeout=60000")
    rows = list(conn.execute("SELECT id, title, url FROM documents WHERE created_at IS NULL OR created_at=''"))
    print("missing created_at:", len(rows), flush=True)
    ok = 0
    fail = 0
    t0 = time.time()
    for i, (doc_id, title, url) in enumerate(rows, 1):
        key = title_key(url, title)
        if not key:
            fail += 1
            continue
        date = first_rev(key)
        if date:
            conn.execute(
                "UPDATE documents SET created_at=? WHERE id=? AND (created_at IS NULL OR created_at='')",
                (date, doc_id),
            )
            ok += 1
        else:
            fail += 1
            print("miss", key, flush=True)
        if i <= 5 or i % 20 == 0 or i == len(rows):
            print(f"{i}/{len(rows)} last={key} date={date} wrote={ok} miss={fail} {int(time.time()-t0)}s", flush=True)
        time.sleep(0.7)
    print("done wrote", ok, "miss", fail, flush=True)
    for r in conn.execute(
        """SELECT topic,
                  SUM(CASE WHEN created_at < '2019-01-01' THEN 1 ELSE 0 END) pre,
                  SUM(CASE WHEN created_at >= '2019-01-01' THEN 1 ELSE 0 END) post,
                  COUNT(*) n
           FROM documents GROUP BY topic ORDER BY n DESC"""
    ):
        print(r, flush=True)


if __name__ == "__main__":
    main()
