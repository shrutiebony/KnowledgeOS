import sqlite3
c = sqlite3.connect(r"C:\Users\sgoya\Downloads\KnowledgeOS\KnowledgeOS\data\knowledgeos.db")
print("datasets:")
for r in c.execute("SELECT id, name, kind, analysis_state FROM datasets"):
    print(r)
print()
print("doc counts by topic:")
for r in c.execute(
    """SELECT topic, COUNT(*),
       SUM(CASE WHEN created_at IS NULL OR created_at='' THEN 1 ELSE 0 END) as missing,
       SUM(CASE WHEN created_at < '2019-01-01' AND created_at IS NOT NULL AND created_at != '' THEN 1 ELSE 0 END) as pre,
       SUM(CASE WHEN created_at >= '2019-01-01' THEN 1 ELSE 0 END) as post
       FROM documents GROUP BY topic ORDER BY COUNT(*) DESC"""
):
    print(r)
print()
print("created_at year hist:")
for r in c.execute("SELECT substr(created_at,1,4) y, COUNT(*) FROM documents GROUP BY y ORDER BY y"):
    print(r)
print()
print("published_at year hist:")
for r in c.execute("SELECT substr(published_at,1,4) y, COUNT(*) FROM documents GROUP BY y ORDER BY y"):
    print(r)
print()
print("sample created_at:")
for r in c.execute("SELECT id, title, topic, created_at, published_at FROM documents LIMIT 20"):
    print(r)
print()
print("total docs", list(c.execute("SELECT COUNT(*) FROM documents"))[0][0])
print("created empty", list(c.execute("SELECT COUNT(*) FROM documents WHERE created_at IS NULL OR created_at=''"))[0][0])
print("created 2026", list(c.execute("SELECT COUNT(*) FROM documents WHERE created_at LIKE '2026%'"))[0][0])
print("created 2025", list(c.execute("SELECT COUNT(*) FROM documents WHERE created_at LIKE '2025%'"))[0][0])
