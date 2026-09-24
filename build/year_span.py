import json
import sqlite3
import urllib.request
from pathlib import Path

root = Path(r"C:\Users\sgoya\Downloads\KnowledgeOS\KnowledgeOS")
c = sqlite3.connect(root / "data" / "knowledgeos.db", timeout=30)
c.execute("PRAGMA busy_timeout=30000")

print("DB years (best date)")
rows = list(
    c.execute(
        """
        select substr(coalesce(nullif(d.created_at, ''), d.published_at), 1, 4) y, count(*)
        from documents d
        where length(coalesce(nullif(d.created_at, ''), d.published_at)) >= 4
        group by y
        order by y
        """
    )
)
for r in rows:
    print(r)
years = [int(r[0]) for r in rows if r[0] and str(r[0]).isdigit()]
post = [y for y in years if y >= 2015]
print("db_min", min(years) if years else None, "db_max", max(years) if years else None)
print("chart_would_be", (2015, max(post) if post else None))

print("--- datasets ---")
for r in c.execute("select id, name from datasets order by id"):
    print(r)
    ds_rows = list(
        c.execute(
            """
            select substr(coalesce(nullif(created_at, ''), published_at), 1, 4) y, count(*)
            from documents
            where dataset_id=? and length(coalesce(nullif(created_at, ''), published_at)) >= 4
            group by y
            order by y
            """,
            (r[0],),
        )
    )
    ys = [int(x[0]) for x in ds_rows if x[0] and str(x[0]).isdigit() and int(x[0]) >= 2015]
    print("  first_any", ds_rows[0][0] if ds_rows else None, "chart", (2015, max(ys) if ys else None))

print("--- API ---")
for path in (
    "http://127.0.0.1:8080/datasets/0/breakdown?by=time",
    "http://127.0.0.1:8080/datasets/1/breakdown?by=time",
):
    try:
        d = json.load(urllib.request.urlopen(path, timeout=30))
        keys = [r.get("key") for r in d.get("rows") or []]
        print(path, "available", d.get("available"), "n", len(keys), "first", keys[:3], "last", keys[-3:] if keys else None)
    except Exception as e:
        print(path, "api_err", e)
