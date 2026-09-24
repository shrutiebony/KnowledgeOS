import sqlite3

c = sqlite3.connect("data/knowledgeos.db")
print("datasets", c.execute("select id,name,kind from datasets").fetchall())
rows = c.execute(
    "select topic, count(*) from documents where dataset_id=1 group by topic order by count(*) desc"
).fetchall()
print("wiki topics", rows)
print("wiki total", c.execute("select count(*) from documents where dataset_id=1").fetchone()[0])
print(
    "australia wiki",
    c.execute(
        "select count(*) from documents where dataset_id=1 and topic='Australia'"
    ).fetchone()[0],
)
