import sqlite3
db = r"C:\Users\sgoya\Downloads\KnowledgeOS\KnowledgeOS\data\knowledgeos.db"
con = sqlite3.connect(db)
cur = con.cursor()
print("wiki", list(cur.execute(
    "SELECT COUNT(*), SUM(CASE WHEN p_ai IS NULL THEN 1 ELSE 0 END), "
    "SUM(CASE WHEN doc.published_at IS NULL OR doc.published_at='' THEN 1 ELSE 0 END), "
    "SUM(CASE WHEN doc.created_at IS NULL OR doc.created_at='' THEN 1 ELSE 0 END) "
    "FROM documents doc JOIN datasets d ON d.id=doc.dataset_id WHERE d.name='Wikipedia'"
))[0])
print("gdelt", list(cur.execute(
    "SELECT COUNT(*), SUM(CASE WHEN p_ai IS NULL THEN 1 ELSE 0 END) "
    "FROM documents doc JOIN datasets d ON d.id=doc.dataset_id WHERE d.name='GDELT'"
))[0])
