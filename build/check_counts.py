import sqlite3

c = sqlite3.connect(r"C:\Users\sgoya\Downloads\KnowledgeOS\KnowledgeOS\data\knowledgeos.db", timeout=10)
print("datasets:")
for r in c.execute("SELECT id,name,kind FROM datasets"):
    print(r)
print("--- wiki topics ---")
for r in c.execute(
    "SELECT topic, COUNT(*) FROM documents WHERE dataset_id=(SELECT id FROM datasets WHERE name='Wikipedia') GROUP BY topic ORDER BY COUNT(*) DESC"
):
    print(r)
print(
    "wiki total",
    c.execute(
        "SELECT COUNT(*) FROM documents WHERE dataset_id=(SELECT id FROM datasets WHERE name='Wikipedia')"
    ).fetchone()[0],
)
print("--- gdelt topics ---")
for r in c.execute(
    "SELECT topic, COUNT(*) FROM documents WHERE dataset_id=(SELECT id FROM datasets WHERE name='GDELT') GROUP BY topic ORDER BY COUNT(*) DESC"
):
    print(r)
print(
    "gdelt total",
    c.execute(
        "SELECT COUNT(*) FROM documents WHERE dataset_id=(SELECT id FROM datasets WHERE name='GDELT')"
    ).fetchone()[0],
)
