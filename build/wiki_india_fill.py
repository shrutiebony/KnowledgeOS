"""Fill Wikipedia collection to 1000 with India / Geography of India extracts."""
import json
import sqlite3
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

DB = r"C:\Users\sgoya\Downloads\KnowledgeOS\KnowledgeOS\data\knowledgeos.db"
UA = "KnowledgeOS-Wiki/1.0 (local educational corpus ingest)"

GEO_CATS = [
    "Category:Districts_of_Andhra_Pradesh",
    "Category:Districts_of_Telangana",
    "Category:Districts_of_Madhya_Pradesh",
    "Category:Districts_of_Odisha",
    "Category:Districts_of_Haryana",
    "Category:Districts_of_Punjab,_India",
    "Category:Cities_in_Uttar_Pradesh",
    "Category:Lakes_of_India",
    "Category:Beaches_of_India",
    "Category:Wildlife_sanctuaries_of_India",
    "Category:Mountain_ranges_of_India",
    "Category:Waterfalls_of_India",
]

LEFTOVER = [
    "Ahmednagar", "Akola", "Ambarnath", "Amravati", "Aurangabad", "Barshi", "Beed",
    "Bhiwandi", "Buldhana", "Chakan, Pune", "Chandrapur", "Dhule", "Gondia",
    "Hinganghat", "Ichalkaranji", "Jalgaon", "Jalna, Maharashtra", "Jawhar",
    "Kalyan-Dombivli", "Kolhapur", "Paratwada", "Parbhani", "Pune", "Ratnagiri",
    "Sangli", "Satara (city)", "Shirdi", "Solapur", "Thane", "Udgir", "Ulhasnagar",
    "Vasai-Virar", "Wardha", "Yavatmal", "Bagalkote", "Ballari", "Belgaum",
    "Bhadravati, Karnataka", "Bidar", "Bijapur", "Chikmagalur", "Chitradurga",
    "Davanagere", "Dharwad", "Gadag-Betageri", "Gangavati, Karnataka", "Hospet",
    "Hubli", "Ilkal", "Kalaburagi", "Kinnigoli", "Kolar, Karnataka", "Mandya",
    "Mangaluru", "Mysore", "Raichur", "Ranebennuru", "Robertsonpet", "Shimoga",
    "Tumkur", "Udupi", "Alipurduar", "Basirhat", "Berhampore", "Bhadreswar, Hooghly",
    "Bishnupur (West Bengal)", "Bolpur", "Champdani", "Chandannagar", "Contai",
    "Cooch Behar", "Darjeeling", "Durgapur", "Haldia", "Howrah", "Jalpaiguri",
    "Kharagpur", "Krishnanagar, Nadia", "Siliguri", "Agra district",
    "Aligarh district", "Ayodhya district", "Azamgarh district", "Bareilly district",
    "Ghaziabad district", "Gorakhpur district", "Jhansi district",
    "Kanpur Nagar district", "Lucknow district", "Mathura district",
    "Meerut district", "Moradabad district", "Prayagraj district",
    "Varanasi district", "Nagpur district", "Nashik district", "Pune district",
    "Thane district", "Gaya district", "Muzaffarpur district", "Nalanda district",
    "Patna district", "Vaishali district", "West Champaran district",
]

GEO_NEEDLES = (
    "district", "city", "town", "village", "state", "geography", "river", "mountain",
    "ghat", "desert", "coast", "island", "park", "climate", "monsoon", "himalay",
    "pradesh", "tamil", "kerala", "karnataka", "maharashtra", "gujarat", "rajasthan",
    "bengal", "mumbai", "chennai", "kolkata", "delhi", "hyderabad", "bengaluru",
    "andhra", "telangana", "odisha", "punjab", "haryana",
)


def log(msg):
    print(msg, flush=True)


def get(url, retries=6):
    delay = 8
    last = None
    for _ in range(retries):
        req = urllib.request.Request(url, headers={"User-Agent": UA})
        try:
            with urllib.request.urlopen(req, timeout=40) as r:
                return r.read().decode("utf-8", "replace")
        except urllib.error.HTTPError as e:
            last = e
            if e.code == 429:
                log("429 backoff %ss" % delay)
                time.sleep(delay)
                delay = min(delay * 2, 60)
                continue
            raise
        except Exception as e:
            last = e
            time.sleep(delay)
            delay = min(delay * 2, 60)
    raise last


def category_members(cat, limit=200):
    titles = []
    cont = ""
    while len(titles) < limit:
        q = {
            "action": "query",
            "format": "json",
            "formatversion": "2",
            "list": "categorymembers",
            "cmtitle": cat.replace("_", " "),
            "cmtype": "page",
            "cmlimit": "100",
        }
        if cont:
            q["cmcontinue"] = cont
        url = "https://en.wikipedia.org/w/api.php?" + urllib.parse.urlencode(q)
        try:
            data = json.loads(get(url))
        except Exception as e:
            log("cat fail %s %s" % (cat, e))
            break
        for m in data.get("query", {}).get("categorymembers", []):
            t = m.get("title") or ""
            if t and ":" not in t:
                titles.append(t)
        cont = data.get("continue", {}).get("cmcontinue", "")
        if not cont:
            break
        time.sleep(0.5)
    return titles


def fetch_extracts(titles):
    q = {
        "action": "query",
        "format": "json",
        "formatversion": "2",
        "prop": "extracts|revisions",
        "explaintext": "1",
        "exsectionformat": "plain",
        "exlimit": str(min(20, len(titles))),
        "redirects": "1",
        "rvprop": "timestamp",
        "titles": "|".join(titles),
    }
    url = "https://en.wikipedia.org/w/api.php?" + urllib.parse.urlencode(q)
    data = json.loads(get(url))
    out = []
    for p in data.get("query", {}).get("pages", []):
        if p.get("missing"):
            continue
        text = (p.get("extract") or "").strip()
        t = p.get("title") or ""
        ts = ""
        revs = p.get("revisions") or []
        if revs:
            ts = (revs[0].get("timestamp") or "")[:10]
        words = len([w for w in text.replace("\n", " ").split(" ") if w])
        if words < 60 or not t:
            continue
        out.append((t, text, words, ts))
    return out


def topic_for(title, text):
    blob = (title + " " + text[:1500]).lower()
    if any(n in blob for n in GEO_NEEDLES):
        return "Geography of India"
    if "india" in blob or "indian" in blob:
        return "India"
    return "Geography of India"


def main():
    con = sqlite3.connect(DB, timeout=60)
    wiki_id = con.execute("SELECT id FROM datasets WHERE name='Wikipedia'").fetchone()[0]
    have = {r[0] for r in con.execute("SELECT url FROM documents WHERE dataset_id=?", (wiki_id,))}
    n = con.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=?", (wiki_id,)).fetchone()[0]
    log("wikipedia %s have urls %s" % (n, len(have)))
    need = 1000 - n
    if need <= 0:
        log("already at 1000")
        return

    titles = list(LEFTOVER)
    for cat in GEO_CATS:
        if len(titles) > 600:
            break
        log("listing %s" % cat)
        titles.extend(category_members(cat, 150))
        time.sleep(0.6)

    added = 0
    seen = set()
    batch = []

    def flush_batch():
        nonlocal added, need
        if not batch or added >= need:
            batch.clear()
            return
        try:
            rows = fetch_extracts(batch[:20])
        except Exception as e:
            log("extract fail %s %s" % (batch[:3], e))
            time.sleep(2)
            batch.clear()
            return
        time.sleep(0.45)
        for t, text, words, ts in rows:
            if added >= need:
                break
            url = "https://en.wikipedia.org/wiki/" + t.replace(" ", "_")
            if url in have:
                continue
            topic = topic_for(t, text)
            if topic not in ("India", "Geography of India"):
                continue
            con.execute(
                "INSERT INTO documents(dataset_id,title,url,source,topic,published_at,created_at,text,word_count) "
                "VALUES(?,?,?,?,?,?,?,?,?)",
                (wiki_id, t, url, "wikipedia.org", topic, ts or None, ts or None, text, words),
            )
            have.add(url)
            added += 1
            if added % 10 == 0:
                con.commit()
                log("added %s %s %s" % (added, t, topic))
        batch.clear()

    for title in titles:
        if added >= need:
            break
        key = title.lower()
        if key in seen:
            continue
        seen.add(key)
        url = "https://en.wikipedia.org/wiki/" + title.replace(" ", "_")
        if url in have:
            continue
        batch.append(title)
        if len(batch) >= 20:
            flush_batch()
    flush_batch()
    con.commit()
    total = con.execute("SELECT COUNT(*) FROM documents WHERE dataset_id=?", (wiki_id,)).fetchone()[0]
    log("done added %s total %s" % (added, total))


if __name__ == "__main__":
    main()
    sys.exit(0)
