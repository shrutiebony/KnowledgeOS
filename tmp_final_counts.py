import sqlite3

con = sqlite3.connect("data/knowledgeos.db", timeout=30)
cur = con.cursor()
print("wiki", dict(cur.execute("SELECT topic, COUNT(*) FROM documents WHERE dataset_id=1 GROUP BY topic")))
print("wiki_n", cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=1").fetchone()[0])
print("gdelt", dict(cur.execute("SELECT topic, COUNT(*) FROM documents WHERE dataset_id=11 GROUP BY topic")))
print("gdelt_n", cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=11").fetchone()[0])
print(
    "india_wiki",
    cur.execute(
        "SELECT COUNT(*) FROM documents WHERE dataset_id=1 AND topic IN ('India','Geography of India')"
    ).fetchone()[0],
)
print(
    "de_wiki",
    cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=1 AND topic='Germany'").fetchone()[0],
)
print(
    "us_wiki",
    cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=1 AND topic='United States'").fetchone()[0],
)
print(
    "au_wiki",
    cur.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=1 AND topic='Australia'").fetchone()[0],
)
