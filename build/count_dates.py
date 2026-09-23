import sqlite3
db = r"C:\Users\sgoya\Downloads\KnowledgeOS\KnowledgeOS\data\knowledgeos.db"
con = sqlite3.connect(db)
cur = con.cursor()
print("wiki missing created_at", list(cur.execute(
    "SELECT COUNT(*) FROM documents doc JOIN datasets d ON d.id=doc.dataset_id "
    "WHERE d.name='Wikipedia' AND (doc.created_at IS NULL OR doc.created_at='')"
))[0][0])
print("wiki total", list(cur.execute(
    "SELECT COUNT(*) FROM documents doc JOIN datasets d ON d.id=doc.dataset_id WHERE d.name='Wikipedia'"
))[0][0])
print("gdelt total", list(cur.execute(
    "SELECT COUNT(*) FROM documents doc JOIN datasets d ON d.id=doc.dataset_id WHERE d.name='GDELT'"
))[0][0])
