import sqlite3

con = sqlite3.connect("data/knowledgeos.db", timeout=30)
cur = con.cursor()

print("=== datasets ===")
for r in cur.execute(
    "SELECT d.id, d.name, d.kind, COUNT(doc.id) "
    "FROM datasets d LEFT JOIN documents doc ON doc.dataset_id=d.id "
    "GROUP BY d.id"
):
    print(r)

print("=== wiki topics ===")
for r in cur.execute(
    "SELECT COALESCE(topic, '(none)'), COUNT(*) FROM documents WHERE dataset_id=1 "
    "GROUP BY topic ORDER BY COUNT(*) DESC"
):
    print(r)

print("=== gdelt topics ===")
for r in cur.execute(
    "SELECT COALESCE(topic, '(none)'), COUNT(*) FROM documents WHERE dataset_id=11 "
    "GROUP BY topic ORDER BY COUNT(*) DESC"
):
    print(r)

print("wiki n", cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=1").fetchone()[0])
print("gdelt n", cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=11").fetchone()[0])
print(
    "india",
    cur.execute(
        "SELECT COUNT(*) FROM documents WHERE dataset_id=1 AND topic IN ('India','Geography of India')"
    ).fetchone()[0],
)
print(
    "us wiki",
    cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=1 AND topic='United States'").fetchone()[0],
)
print(
    "de wiki",
    cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=1 AND topic='Germany'").fetchone()[0],
)
print(
    "au wiki",
    cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=1 AND topic='Australia'").fetchone()[0],
)
print(
    "gdelt countries",
    {
        k: cur.execute(
            "SELECT COUNT(*) FROM documents WHERE dataset_id=11 AND topic=?", (k,)
        ).fetchone()[0]
        for k in ("United States", "Germany", "Australia", "India", "Geography of India")
    },
)
