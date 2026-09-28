import sqlite3
c = sqlite3.connect(r"C:\Users\sgoya\Downloads\KnowledgeOS\KnowledgeOS\data\knowledgeos.db", timeout=10)
c.execute("PRAGMA busy_timeout=10000")
print("DATASETS")
for r in c.execute(
    "select id,name,kind,analysis_state,"
    "(select count(*) from documents d where d.dataset_id=datasets.id) "
    "from datasets"
):
    print(r)
print("TOPICS_BY_DATASET")
for r in c.execute(
    "select ds.name, d.topic, count(*) "
    "from documents d join datasets ds on ds.id=d.dataset_id "
    "group by ds.name, d.topic order by ds.name, count(*) desc"
):
    print(r)
