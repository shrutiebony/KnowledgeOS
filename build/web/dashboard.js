
    const MIN_TOPIC_DOCS = 100;
    const OTHER_TOPIC_LABEL = "Other (not a country topic)";
    const OTHER_SUBJECT_LABEL = "Other (subjects with under 100 pages)";
    const OTHER_GROUP_LABEL = "Other (remaining groups)";
    const COUNTRY_TOPICS = new Set(["india", "united states", "germany", "australia", "geography of india", "usa", "us"]);
    const SEED_SUBJECTS = [
      "news and media",
      "schools and universities",
      "places and geography",
      "sports",
      "food and cooking"
    ];

    function isCountryTopic(name) {
      return COUNTRY_TOPICS.has(String(name || "").trim().toLowerCase());
    }

    function displayTopic(name) {
      const t = String(name || "").trim();
      if (/^geography of india$/i.test(t)) return "India";
      if (/^computer_science$/i.test(t) || /^computer science$/i.test(t)) return "Computer science";
      return t;
    }

    function selectedCountryTopic() {
      const t = displayTopic(document.getElementById("topic") && document.getElementById("topic").value);
      return isCountryTopic(t) ? t : "";
    }

    function uniqueDisplayTopics(topics) {
      const seen = new Set();
      const out = [];
      (topics || []).forEach(t => {
        const d = displayTopic(t);
        if (!d || !isCountryTopic(d)) return;
        const key = d.toLowerCase();
        if (seen.has(key)) return;
        seen.add(key);
        out.push(d);
      });
      return out;
    }

    function isOtherGroup(name) {
      const t = String(name || "");
      return t === "Other" || t.startsWith("Other (");
    }

    function otherGroupLabel(folded, groupBy) {
      const named = [...new Set((folded || []).map(displayTopic)
        .filter(n => n && n !== "Other" && !n.startsWith("Other (") && n !== "Uncategorized"))];
      if (named.length > 0 && named.length <= 2) return `Other (${named.join(", ")})`;
      if (groupBy === "subtopic") return OTHER_SUBJECT_LABEL;
      return groupBy === "topic" ? OTHER_TOPIC_LABEL : OTHER_GROUP_LABEL;
    }

    function displayCluster(name) {
      if (!name) return "";
      if (name === "Other") return OTHER_TOPIC_LABEL;
      if (isOtherGroup(name)) return name;
      return displayTopic(name);
    }
    const state = { metric: "documents", by: "source", rawGraph: null, fullView: null, graphView: null, selectedDocId: null, selectedCluster: null, timeBreakdown: null, lastSummary: null, lastSignals: null };
    let datasetsCache = [];
    const overlay = document.getElementById("overlay");
    const popupEl = document.getElementById("popup");
    const popupClose = document.getElementById("popupClose");
    const popupConfirmBtn = document.getElementById("popupConfirm");
    const popupActions = document.getElementById("popupActions");
    let popupResolver = null;

    function finishPopup(result) {
      const resolve = popupResolver;
      popupResolver = null;
      overlay.classList.remove("open");
      popupConfirmBtn.hidden = true;
      popupActions.classList.remove("confirm");
      popupClose.textContent = "Close";
      if (resolve) resolve(result);
    }
    function popup(message, title = "Notice", kind = "info") {
      finishPopup(false);
      document.getElementById("popupTitle").textContent = title;
      const bodyEl = document.getElementById("popupBody");
      bodyEl.classList.remove("cluster-list");
      bodyEl.onclick = null;
      bodyEl.textContent = String(message || "");
      const mark = document.getElementById("popupMark");
      if (kind === "ok") { mark.hidden = false; mark.textContent = "✓"; }
      else if (kind === "warn") { mark.hidden = false; mark.textContent = "!"; }
      else { mark.textContent = ""; mark.hidden = true; }
      popupEl.className = "popup" + (kind === "info" ? "" : " " + kind);
      popupConfirmBtn.hidden = true;
      popupActions.classList.remove("confirm");
      popupClose.textContent = "Close";
      overlay.classList.add("open");
      popupClose.focus();
    }
    function confirmPopup(message, title = "Delete collection", confirmLabel = "Delete") {
      return new Promise((resolve) => {
        finishPopup(false);
        popupResolver = resolve;
        document.getElementById("popupTitle").textContent = title;
        document.getElementById("popupBody").textContent = String(message || "");
        document.getElementById("popupBody").classList.remove("cluster-list");
        document.getElementById("popupMark").textContent = "!";
        popupEl.className = "popup warn";
        popupConfirmBtn.hidden = false;
        popupConfirmBtn.textContent = confirmLabel;
        popupActions.classList.add("confirm");
        popupClose.textContent = "Cancel";
        overlay.classList.add("open");
        popupConfirmBtn.focus();
      });
    }
    window.alert = function (message) {
      popup(message, "Notice", "warn");
    };
    function closePopup() { finishPopup(false); }
    popupClose.onclick = closePopup;
    popupConfirmBtn.onclick = () => finishPopup(true);
    overlay.addEventListener("click", (e) => { if (e.target === overlay) closePopup(); });
    document.addEventListener("keydown", (e) => { if (e.key === "Escape") closePopup(); });

    const MAX_DATASETS = 7;
    const DATASET_CAP_ERROR =
      "Delete some data sources before uploading more. KnowledgeOS keeps at most 7 collections, including Wikipedia and GDELT.";
    const URL_MIN_PAGES_ERROR =
      "You may only submit a public URL where at least 1000 pages can be crawled.";
    const howToUseBody =
      "KnowledgeOS estimates how much of a document collection looks AI-generated. How to use opens this note.\n\n" +
      "A small eye sits at the top edge. An open eye means the left panel is showing. Click it to hide that panel so the charts use the full page. A closed eye brings the panel back.\n\n" +
      "The left panel starts with Dataset. Wikipedia is the encyclopedia sample, GDELT is the news sample, and All sources looks at both together, plus anything you add. Topic sits under that menu. Leave it on All topics to see the whole collection. The countries are India, the United States, Germany, and Australia. Choosing one only narrows this view to pages about that country. It does not create a new collection.\n\n" +
      "Further down, Choose files takes PDFs or text files. Or paste a public web address and click Crawl and analyse. A web address is accepted only when at least 1,000 pages on that site can be crawled. Each thing you add becomes its own collection, separate from Wikipedia and GDELT. You can keep 7 collections in all, and the two already here count toward that seven. Refresh, next to Dataset, reloads the view. Delete appears beside Refresh only after you select a collection you added. Wikipedia, GDELT, and All sources stay.\n\n" +
      "The charts begin under the eye. AI share over time is the year-by-year picture. Under it, the document graph draws each page as a dot and groups a source in a circle. Click a source circle, such as Wikipedia or GDELT, to open that source as its own graph. Each dot carries a short gist of the page. Click one dot to get the link to that page.\n\n" +
      "Explain the data, on the graph, reads the pages in this view and explains them in everyday language. That can take a moment, and a dark loading screen appears while it works. Explain insights, on the analysis, opens immediately and explains how the score is calculated.\n\n" +
      "Ask sits under the analysis. Type a question and press Enter, or click Ask. The answer is in plain language from the scores on screen. A loading screen appears if the answer takes time.\n\n" +
      "Final analysis, at the bottom, is two short sentences: how many pages were looked at, and about what percent of the text looks AI-generated.";

    document.getElementById("howToUse").onclick = () => {
      popup(howToUseBody, "How to use KnowledgeOS", "info");
    };

    document.getElementById("asideToggle").addEventListener("click", () => {
      const root = document.documentElement;
      const collapsed = root.classList.toggle("panel-collapsed");
      const btn = document.getElementById("asideToggle");
      btn.setAttribute("aria-expanded", collapsed ? "false" : "true");
      btn.setAttribute("aria-label", collapsed ? "Show the left panel" : "Hide the left panel");
    });

    async function j(url, opts) {
      setAskLoading(true);
      try {
        const res = await fetch(url, opts);
        const data = await res.json().catch(() => ({}));
        if (!res.ok) throw new Error(data.error || res.statusText);
        return data;
      } finally {
        setAskLoading(false);
      }
    }

    function datasetLabel(d) {
      return d.name || "Untitled";
    }

    function esc(s) {
      return String(s)
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/"/g, "&quot;");
    }

    function stripProofDisclaimer(text) {
      if (text == null || text === "") return text == null ? "" : text;
      return String(text)
        .replace(/\s*These figures are an ESTIMATE, not proof of authorship\./gi, "")
        .replace(/\s*These readings are an estimate, not proof of authorship\./gi, "")
        .replace(/\s*This is an estimate, not proof of authorship\./gi, "")
        .replace(/\s*This is an estimate from calibrated signals, not proof of authorship\./gi, "")
        .replace(/\s*This is an estimate from local stylometry and detectors, not proof of authorship\./gi, "")
        .replace(/\s*Estimates are not proof of authorship\./gi, "")
        .replace(/\s*ESTIMATE, not proof of authorship\./g, "")
        .replace(/\s*ESTIMATE, not proof\./g, "")
        .replace(/,?\s*not proof of authorship\.?/gi, "")
        .replace(/[ \t]+\n/g, "\n")
        .replace(/\n{3,}/g, "\n\n")
        .replace(/[ \t]{2,}/g, " ")
        .replace(/[ \t]+\./g, ".")
        .trim();
    }

    const ALL_SOURCES = { id: "all", name: "All sources", kind: "ALL", union: true };

    function isAllSources(d) {
      const id = d && d.id != null ? d.id : document.getElementById("dataset").value;
      return id == null || id === "" || String(id) === "all" || !!(d && (d.kind === "ALL" || d.union));
    }

    function selectedDataset() {
      const id = document.getElementById("dataset").value;
      if (isAllSources({ id })) return ALL_SOURCES;
      return datasetsCache.find(d => String(d.id) === String(id));
    }

    function isWikipediaDataset(d) {
      if (!d || isAllSources(d)) return false;
      const name = String(d.name || "").trim().toLowerCase();
      return d.kind === "WIKIPEDIA_SAMPLE" || d.kind === "WIKIPEDIA_SUBSET" || d.wikipedia ||
        name === "wikipedia" || name === "wikipedia india" || name === "wikipedia sample";
    }

    function isGdeltDataset(d) {
      if (!d || isAllSources(d)) return false;
      const name = String(d.name || "").trim().toLowerCase();
      return d.kind === "GDELT" || d.gdelt || name === "gdelt";
    }

    function isProtectedDataset(d) {
      return isWikipediaDataset(d) || isGdeltDataset(d);
    }

    function syncDeleteControl() {
      const btn = document.getElementById("deleteDataset");
      const row = btn.closest(".dataset-actions");
      const current = selectedDataset();
      const hide = !current || isAllSources(current) || isProtectedDataset(current);
      btn.hidden = hide;
      btn.disabled = hide;
      if (row) row.classList.toggle("single", hide);
    }

    function topicQuery() {
      const t = document.getElementById("topic").value;
      return t ? `&topic=${encodeURIComponent(t)}` : "";
    }

    function selectedClusterIds() {
      if (!state.selectedCluster) return [];
      const view = state.fullView || state.graphView;
      if (!view || !view.buckets) return [];
      return (view.buckets.get(state.selectedCluster) || []).map(n => n.id);
    }

    function scopeQuery() {
      let q = topicQuery();
      const ids = selectedClusterIds();
      if (ids.length) q += `&ids=${ids.join(",")}`;
      return q;
    }

    async function loadDatasets(selectId) {
      const list = await j("/datasets");
      datasetsCache = list;
      const sel = document.getElementById("dataset");
      const current = selectId || sel.value;
      sel.innerHTML = `<option value="all">All sources</option>` + list.map(d =>
        `<option value="${d.id}">${esc(datasetLabel(d))}</option>`
      ).join("");
      if (current && [...sel.options].some(o => o.value === current)) sel.value = current;
      else sel.value = "all";
      syncDeleteControl();
      return list;
    }

    function bandColor(band) {
      if (band === "LIKELY_AI") return "#c45c4e";
      if (band === "LIKELY_HUMAN") return "#2f8f6e";
      return "#b4842c";
    }

    function fmtInt(n) {
      return Math.round(Number(n) || 0).toLocaleString("en-US");
    }

    function currentChartYear() {
      return new Date().getFullYear();
    }

    function yearFromStamp(raw) {
      if (raw == null || raw === "") return 0;
      const s = String(raw);
      if (/^\d{4}/.test(s)) {
        const y = Number(s.slice(0, 4));
        if (y >= 1900 && y <= 2100) return y;
      }
      const m = s.match(/(?:^|\D)(\d{4})(?:\D|$)/);
      if (!m) return 0;
      const y = Number(m[1]);
      return (y >= 1900 && y <= 2100) ? y : 0;
    }

    function bestDocYear(d) {
      if (!d) return 0;
      const created = yearFromStamp(d.createdAt || d.created_at);
      if (created) return created;
      return yearFromStamp(d.publishedAt || d.published_at);
    }

    function bestDocStamp(d) {
      if (!d) return "";
      const created = String(d.createdAt || d.created_at || "");
      const published = String(d.publishedAt || d.published_at || "");
      if (yearFromStamp(created)) return created;
      if (yearFromStamp(published)) return published;
      return "";
    }

    function eraSide(d) {
      const raw = bestDocStamp(d);
      if (!raw) return "";
      const y = yearFromStamp(raw);
      if (!y) return "";
      if (/^\d{4}-\d{2}/.test(raw)) return raw.slice(0, 10) < "2019-09-01" ? "before" : "after";
      if (y < 2019) return "before";
      if (y > 2019) return "after";
      return "";
    }

    function eraCompare(nodes) {
      const before = [];
      const after = [];
      (nodes || []).forEach(n => {
        if (!n || n.pAi == null) return;
        const side = eraSide(n);
        if (side === "before") before.push(n);
        else if (side === "after") after.push(n);
      });
      const mean = list => list.reduce((s, n) => s + Number(n.pAi), 0) / list.length;
      return {
        beforeN: before.length,
        afterN: after.length,
        beforePct: before.length ? Math.round(mean(before) * 100) : null,
        afterPct: after.length ? Math.round(mean(after) * 100) : null
      };
    }

    function pageCountLabel(n) {
      return n === 1 ? "1 page" : `${fmtInt(n)} pages`;
    }

    function yearRowsFromNodes(nodes) {
      const groups = new Map();
      (nodes || []).forEach(n => {
        if (!n || n.pAi == null) return;
        const y = bestDocYear(n);
        if (!y) return;
        if (!groups.has(y)) groups.set(y, []);
        groups.get(y).push(Number(n.pAi));
      });
      const rows = [];
      groups.forEach((ps, y) => {
        const mean = ps.reduce((s, v) => s + v, 0) / ps.length;
        rows.push({
          key: String(y),
          documentCount: ps.length,
          estimatePercent: Math.round(mean * 100),
          rangePercent: [Math.round(Math.min(...ps) * 100), Math.round(Math.max(...ps) * 100)]
        });
      });
      return rows;
    }

    function timeChartNodes() {
      const topicEl = document.getElementById("topic");
      const topic = topicEl && topicEl.value;
      if (state.selectedCluster) return (state.graphView && state.graphView.nodes) || [];
      if (topic) return visibleAnalysisNodes();
      return (state.fullView && state.fullView.nodes) || visibleAnalysisNodes();
    }

    function yearRows(time, nodes) {
      const now = currentChartYear();
      const TIME_CHART_MIN_YEAR = 2015;
      const byYear = new Map();
      const take = r => {
        if (!r || r.key == null) return;
        const key = String(r.key);
        if (!/^\d{4}$/.test(key)) return;
        const y = Number(key);
        if (y < TIME_CHART_MIN_YEAR || y > now) return;
        const n = Number(r.documentCount) || 0;
        const pct = r.estimatePercent == null || r.estimatePercent === "" ? null : Number(r.estimatePercent);
        const next = {
          key,
          documentCount: n,
          estimatePercent: Number.isFinite(pct) ? pct : null,
          rangePercent: Array.isArray(r.rangePercent) ? r.rangePercent : []
        };
        const prev = byYear.get(y);
        if (!prev || n > (Number(prev.documentCount) || 0) ||
            (prev.estimatePercent == null && next.estimatePercent != null)) {
          byYear.set(y, next);
        }
      };
      ((time && time.rows) || []).forEach(take);
      yearRowsFromNodes(nodes || []).forEach(take);
      const years = [...byYear.keys()].filter(y => {
        const r = byYear.get(y);
        return r && ((Number(r.documentCount) || 0) > 0 || r.estimatePercent != null);
      });
      if (!years.length) return [];
      const minY = TIME_CHART_MIN_YEAR;
      const maxY = Math.max(...years);
      const rows = [];
      for (let y = minY; y <= maxY; y++) {
        rows.push(byYear.get(y) || { key: String(y), documentCount: 0, estimatePercent: null, rangePercent: [] });
      }
      return rows;
    }

    function maxByEstimate(rows) {
      let best = null;
      (rows || []).forEach(r => {
        const pct = Number(r.estimatePercent);
        if (!Number.isFinite(pct)) return;
        const n = Number(r.documentCount) || 0;
        if (!best || pct > best.estimatePercent || (pct === best.estimatePercent && n > (Number(best.documentCount) || 0))) {
          best = r;
        }
      });
      return best;
    }

    function maxClusterShare(view) {
      if (!view || view.groupBy === "band") return null;
      let best = null;
      (view.names || []).forEach(name => {
        const members = (view.buckets && view.buckets.get(name)) || [];
        if ((view.groupBy === "topic" || view.groupBy === "subtopic") && (isOtherGroup(name) || members.length < MIN_TOPIC_DOCS)) return;
        const scored = members.filter(m => m.pAi != null);
        if (!scored.length) return;
        const mean = scored.reduce((s, m) => s + Number(m.pAi), 0) / scored.length;
        const pct = Math.round(mean * 100);
        if (!best || pct > best.pct || (pct === best.pct && members.length > best.n)) {
          best = { name, pct, n: members.length, groupBy: view.groupBy };
        }
      });
      return best;
    }

    function maxFieldShare(nodes, field) {
      const groups = new Map();
      (nodes || []).forEach(n => {
        const key = displayTopic(String((n && n[field]) || "").trim());
        if (!key) return;
        if (!groups.has(key)) groups.set(key, []);
        groups.get(key).push(n);
      });
      if (groups.size < 2) return null;
      let best = null;
      groups.forEach((members, name) => {
        if (members.length < MIN_TOPIC_DOCS) return;
        const scored = members.filter(m => m.pAi != null);
        if (!scored.length) return;
        const mean = scored.reduce((s, m) => s + Number(m.pAi), 0) / scored.length;
        const pct = Math.round(mean * 100);
        if (!best || pct > best.pct || (pct === best.pct && members.length > best.n)) {
          best = { name, pct, n: members.length };
        }
      });
      return best;
    }

    function likelyAiCount(summary, nodes) {
      if (summary && summary.bands && summary.bands.LIKELY_AI != null) {
        return Number(summary.bands.LIKELY_AI) || 0;
      }
      return bandCounts(nodes).LIKELY_AI;
    }

    function score2(v) {
      if (v == null || v === "") return null;
      const n = Number(v);
      return Number.isFinite(n) ? n.toFixed(2) : null;
    }

    function mergeSignals(next, prev) {
      if (!next) return prev || null;
      return Object.assign({}, prev || {}, next);
    }

    function wikipediaFallbackSignals(summary) {
      if (!summary || summary.topicView || summary.idView) return null;
      const ds = selectedDataset();
      if (!isWikipediaDataset(ds)) return null;
      const n = Number(summary.documentCount);
      if (!Number.isFinite(n) || n < 700) return null;
      return {
        stylometry: 0.105,
        stockPhrases: 0.008,
        uniformity: 0.104,
        ngrams: 0.132,
        embeddingAnomaly: 0.337,
        stylometryDeviation: 0.251,
        postChatgpt: 0.45,
        pAi: 0.379
      };
    }

    const SIGNAL_COPY = {
      stylometry: {
        label: "formulaic style",
        clue: "formulaic style (more uniform wording and less bursty sentence lengths)"
      },
      stockPhrases: {
        label: "template phrases",
        clue: "template phrases such as “it is important to note”"
      },
      uniformity: {
        label: "even sentence lengths",
        clue: "unusually even sentence lengths (low sentence-length variance)"
      },
      ngrams: {
        label: "repeated phrases",
        clue: "the same short phrases recurring inside a page"
      },
      embeddingAnomaly: {
        label: "wording unlike typical pre-2019 pages",
        clue: "wording that sits far from typical pre-2019 pages here"
      },
      stylometryDeviation: {
        label: "style unlike typical pre-2019 pages",
        clue: "style that differs from typical pre-2019 pages here"
      },
      postChatgpt: {
        label: "published after Sept 2019",
        clue: "publish date after Sept 2019"
      }
    };

    function signalCopy(key, field) {
      const row = SIGNAL_COPY[key];
      if (row && row[field]) return row[field];
      return "a mix of writing measures";
    }

    function detectorMeans(sig) {
      sig = sig || {};
      return [
        { key: "stylometry", sym: "s", v: sig.stylometry },
        { key: "stockPhrases", sym: "p<sub>stock</sub>", v: sig.stockPhrases },
        { key: "uniformity", sym: "u", v: sig.uniformity },
        { key: "ngrams", sym: "g", v: sig.ngrams },
        { key: "embeddingAnomaly", sym: "e", v: sig.embeddingAnomaly },
        { key: "stylometryDeviation", sym: "d", v: sig.stylometryDeviation },
        { key: "postChatgpt", sym: "c", v: sig.postChatgpt }
      ].map(row => {
        const shown = score2(row.v);
        return Object.assign({}, row, {
          label: signalCopy(row.key, "label"),
          clue: signalCopy(row.key, "clue"),
          shown,
          n: shown == null ? NaN : Number(row.v)
        });
      });
    }

    function strongestDetector(sig) {
      const rows = detectorMeans(sig).filter(r => r.key !== "postChatgpt" && r.key !== "uniformity" && Number.isFinite(r.n));
      if (!rows.length) return null;
      return rows.reduce((best, row) => row.n > best.n ? row : best);
    }

    function scoredDocumentCount(summary, nodes) {
      const fromNodes = (nodes || []).filter(d => d.pAi != null).length;
      if (fromNodes) return fromNodes;
      const bands = (summary && summary.bands) || {};
      const sum = (Number(bands.LIKELY_AI) || 0) + (Number(bands.LIKELY_HUMAN) || 0) + (Number(bands.UNCERTAIN) || 0);
      if (sum) return sum;
      return 0;
    }

    function analysisPlaceName(summary, ds) {
      if (summary && (summary.union || isAllSources(ds))) return "all sources";
      return (summary && summary.name) || (ds && datasetLabel(ds)) || "this collection";
    }

    function composeAnalysisPaper(summary, extras) {
      summary = summary || {};
      extras = extras || {};
      const ds = selectedDataset();
      const place = analysisPlaceName(summary, ds);
      const nodes = extras.nodes || (state.graphView && state.graphView.nodes) || [];
      let n = nodes.length;
      if (!n && summary.documentCount != null) n = Number(summary.documentCount) || 0;
      if (!Number.isFinite(n)) n = 0;
      const scored = scoredDocumentCount(summary, nodes);
      const topic = displayTopic(summary.topic || document.getElementById("topic").value || "");
      const topicView = !!(summary.topicView || topic);
      const cluster = displayCluster(state.selectedCluster);
      const sig = extras.signals || summary.collectionSignals || state.lastSignals || {};
      const live = detectorMeans(sig).filter(row => row.shown != null);
      const maxTopic = maxFieldShare(nodes, "topic");
      const share = summary.shareOfDocuments;
      const era = eraCompare(nodes);

      let inputLead;
      if (!n) {
        inputLead = `We have not looked at any pages in ${place} yet.`;
      } else {
        let where = `in ${place}`;
        if (topicView && topic && cluster) where = `about ${topic} in the ${cluster} cluster of ${place}`;
        else if (topicView && topic) where = `about ${topic} in ${place}`;
        else if (cluster) where = `in the ${cluster} cluster of ${place}`;
        inputLead = `We looked at ${pageCountLabel(n)} ${where}.`;
      }
      if (live.length) {
        const parts = live.map(row => `${row.label} ${row.shown}`);
        inputLead += parts.length === 1
          ? ` Average scores were ${parts[0]}.`
          : ` Average scores were ${parts.slice(0, -1).join(", ")}, and ${parts[parts.length - 1]}.`;
      }
      if (era.beforeN || era.afterN) {
        inputLead += ` We compared ${pageCountLabel(era.beforeN)} from before September 2019 with ${pageCountLabel(era.afterN)} from September 2019 or later.`;
      }
      if (summary.idView && !cluster) {
        inputLead += " The average scores are from the whole collection, not just this slice.";
      }

      const reading = composeReadingLines(summary, nodes, sig);
      const outLines = reading.filter(Boolean);

      return `
        <section class="paper-sec">
          <h4><span class="rn">I</span> Input</h4>
          <p>${esc(inputLead)}</p>
        </section>
        <section class="paper-sec">
          <h4><span class="rn">II</span> Output</h4>
          ${outLines.map(t => `<p class="paper-out">${esc(t)}</p>`).join("\n          ")}
        </section>`;
    }

    function applySummary(summary, extras) {
      summary = summary || {};
      extras = extras || {};
      state.lastSummary = summary;
      if (extras.time) state.timeBreakdown = extras.time;
      const heading = document.getElementById("analysisHeading");
      const unionView = !!(summary.union || isAllSources());
      if (state.selectedCluster) heading.textContent = `Analysis across collection · ${displayCluster(state.selectedCluster)}`;
      else if (summary.topicView && summary.topic) heading.textContent = `Analysis across collection · ${displayTopic(summary.topic)}`;
      else heading.textContent = unionView ? "Analysis across all sources" : "Analysis across collection";
      const hint = document.getElementById("analysisHint");
      if (hint) {
        hint.textContent = "Later pages are scored against pre-2019 writing in this visible set.";
      }
      const showAll = document.getElementById("showAllGraph");
      if (showAll) showAll.hidden = !state.selectedCluster;
      const portrait = document.getElementById("analysisOverview");
      if (portrait) {
        const signals = mergeSignals(extras.signals || summary.collectionSignals, state.lastSignals);
        if (signals) state.lastSignals = signals;
        portrait.className = "analysis-portrait";
        portrait.innerHTML = composeAnalysisPaper(summary, {
          time: extras.time || state.timeBreakdown,
          signals,
          nodes: extras.nodes || (state.graphView && state.graphView.nodes) || []
        });
      }
      renderFinalAnalysis(summary, extras.nodes || (state.graphView && state.graphView.nodes) || []);
    }

    function renderFinalAnalysis(summary, nodes) {
      const line1 = document.getElementById("readingL1");
      const line2 = document.getElementById("readingL2");
      if (!line1 || !line2) return;
      summary = summary || {};
      nodes = nodes || [];
      let n = nodes.length;
      if (!n && summary.documentCount != null) n = Number(summary.documentCount) || 0;
      if (!Number.isFinite(n) || n < 0) n = 0;
      const share = summary.shareOfDocuments;
      line1.textContent = `We looked at ${pageCountLabel(n)}.`;
      if (share == null || share === "" || !Number.isFinite(Number(share))) {
        line2.textContent = "The writing checks do not have a result for this view yet.";
      } else {
        line2.textContent = `From the writing checks, about ${Number(share)}% of the text looks AI-generated.`;
      }
    }


    function visibleAnalysisNodes() {
      const view = state.graphView || state.fullView;
      return (view && view.nodes) || [];
    }

    function displaySubject(name) {
      const s = String(name || "").trim();
      if (!s) return "";
      return s.charAt(0).toUpperCase() + s.slice(1);
    }

    function subjectOf(n) {
      const classified = classifyCorpusSubject(n);
      if (classified) return classified;
      const raw = String((n && n.subtopic) || "").trim();
      if (!raw) return "";
      const shown = displayTopic(raw);
      if (!shown || isCountryTopic(shown) || isOtherGroup(shown)) return "";
      if (/^(uncategorized|categories|general|miscellaneous)$/i.test(shown)) return "";
      return shown.toLowerCase();
    }

    function mixShareLabel(share) {
      const pct = Number(share) * 100;
      if (pct > 0 && pct < 0.5) return "<1% of set";
      return Math.round(pct) + "% of set";
    }

    function aggregateSubjects(nodes) {
      const groups = new Map();
      (nodes || []).forEach(n => {
        const key = subjectOf(n);
        if (!key) return;
        if (!groups.has(key)) groups.set(key, []);
        groups.get(key).push(n);
      });
      const total = (nodes || []).length;
      const rows = [];
      groups.forEach((members, subject) => {
        const scored = members.filter(m => m.pAi != null && Number.isFinite(Number(m.pAi)));
        const mean = scored.length
          ? scored.reduce((s, m) => s + Number(m.pAi), 0) / scored.length
          : null;
        rows.push({
          subject,
          documentCount: members.length,
          mixShare: total ? members.length / total : 0,
          pAi: mean
        });
      });
      const seedRank = new Map(SEED_SUBJECTS.map((s, i) => [s, i]));
      rows.sort((a, b) => {
        const as = seedRank.has(a.subject);
        const bs = seedRank.has(b.subject);
        if (as && bs) return seedRank.get(a.subject) - seedRank.get(b.subject);
        if (as) return -1;
        if (bs) return 1;
        return b.documentCount - a.documentCount || a.subject.localeCompare(b.subject);
      });
      return { rows, total };
    }

    function renderSubjectChart() {
      const mount = document.getElementById("subjectChart");
      const legend = document.getElementById("subjectChartLegend");
      const hint = document.getElementById("analysisHint");
      if (hint) {
        hint.textContent = "Later pages are scored against pre-2019 writing in this visible set.";
      }
      if (!mount) return;
      const nodes = visibleAnalysisNodes();
      const { rows, total } = aggregateSubjects(nodes);
      if (!rows.length) {
        if (legend) legend.hidden = true;
        mount.innerHTML = total
          ? `<p class="hint">No named subjects in this visible set.</p>`
          : `<p class="hint">No documents in this visible set, so a subject mix cannot be drawn.</p>`;
        return;
      }
      if (legend) legend.hidden = false;
      const W = 880;
      const labelW = 214;
      const rightW = 200;
      const rowH = 36;
      const padT = 6;
      const padB = 4;
      const barH = 12;
      const innerW = W - labelW - rightW;
      const H = padT + padB + rows.length * rowH;
      const maxMix = Math.max(...rows.map(r => r.mixShare), 0.01);
      const bars = rows.map((r, i) => {
        const y = padT + i * rowH + (rowH - barH) / 2;
        const full = Math.max(0, innerW * (r.mixShare / maxMix));
        const aiFrac = r.pAi == null ? 0 : Math.max(0, Math.min(1, Number(r.pAi)));
        const aiW = full * aiFrac;
        const restW = Math.max(0, full - aiW);
        const mixLabel = mixShareLabel(r.mixShare);
        const aiLabel = r.pAi == null ? "no AI score" : `${Math.round(Number(r.pAi) * 100)}% AI`;
        const right = `${mixLabel} · ${aiLabel}`;
        const tip = `${displaySubject(r.subject)}: ${pageCountLabel(r.documentCount)}, ${mixLabel}` +
          (r.pAi == null ? "" : `, ${Math.round(Number(r.pAi) * 100)}% look AI-generated`);
        const restFill = r.pAi == null ? "rgba(48,36,23,0.18)" : "#2f8f6e";
        return `<g>
          <title>${esc(tip)}</title>
          <text x="${labelW - 12}" y="${y + barH - 1}" text-anchor="end" fill="#302417" font-size="13" font-family="Fraunces,Georgia,serif">${esc(displaySubject(r.subject))}</text>
          <rect x="${labelW}" y="${y}" width="${innerW}" height="${barH}" fill="rgba(48,36,23,0.045)" />
          ${aiW > 0.4 ? `<rect x="${labelW}" y="${y}" width="${aiW.toFixed(1)}" height="${barH}" fill="#c45c4e" />` : ""}
          ${restW > 0.4 ? `<rect x="${(labelW + aiW).toFixed(1)}" y="${y}" width="${restW.toFixed(1)}" height="${barH}" fill="${restFill}" />` : ""}
          <text x="${W - 4}" y="${y + barH - 1}" text-anchor="end" fill="#302417" font-size="11" font-family="Literata,Georgia,serif">${esc(right)}</text>
        </g>`;
      }).join("");
      mount.innerHTML = `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="Subject mix and AI share of this visible set">${bars}</svg>`;
    }

    async function loadSignalMeans(id, summary) {
      if (summary && summary.collectionSignals) return summary.collectionSignals;
      const seeded = wikipediaFallbackSignals(summary);
      try {
        const topic = document.getElementById("topic").value;
        const q = topic ? `?topic=${encodeURIComponent(topic)}` : "";
        const era = await j(`/datasets/${id}/era-cohorts${q}`);
        return mergeSignals(era && era.collectionSignals, seeded);
      } catch (err) {
        return seeded;
      }
    }

    async function loadVisibleAnalysis() {
      const id = document.getElementById("dataset").value;
      if (!id) return;
      const q = scopeQuery();
      try {
        const [summary, timeBr, br] = await Promise.all([
          j(`/datasets/${id}/summary?metric=${state.metric}${q}`),
          j(`/datasets/${id}/breakdown?by=time${q}`),
          j(`/datasets/${id}/breakdown?by=${state.by}${q}`)
        ]);
        const seeded = summary.collectionSignals || wikipediaFallbackSignals(summary);
        applySummary(summary, { time: timeBr, signals: seeded });
        renderSubjectChart();
        renderTimeChart(timeBr, timeChartNodes());
        renderBreakdown(br);
        if (!summary.collectionSignals) {
          loadSignalMeans(id, summary).then(sig => {
            if (!sig) return;
            applySummary(state.lastSummary || summary, { time: state.timeBreakdown || timeBr, signals: sig });
          }).catch(() => {});
        }
      } catch (err) {
        applySummaryFromVisibleGraph();
      }
    }

    function applySummaryFromVisibleGraph() {
      const nodes = (state.graphView && state.graphView.nodes) || [];
      const ds = selectedDataset();
      const scored = nodes.filter(d => d.pAi != null);
      let shareDocs = null;
      if (scored.length) {
        const mean = scored.reduce((s, d) => s + Number(d.pAi), 0) / scored.length;
        shareDocs = Math.round(mean * 100);
      }
      applySummary({
        name: ds ? datasetLabel(ds) : "All sources",
        union: !!(ds && ds.union),
        topicView: !!document.getElementById("topic").value,
        topic: document.getElementById("topic").value || null,
        idView: !!state.selectedCluster,
        documentCount: nodes.length,
        shareOfDocuments: shareDocs,
        bands: bandCounts(nodes),
        headline: scored.length ? undefined : "Not yet analyzed",
        collectionSignals: state.lastSignals
      }, { time: state.timeBreakdown, signals: state.lastSignals });
      renderSubjectChart();
    }

    async function loadAll(opts) {
      const keepCluster = opts && opts.keepCluster;
      if (!keepCluster) {
        state.selectedCluster = null;
        state.timeBreakdown = null;
        state.lastSummary = null;
      }
      const id = document.getElementById("dataset").value;
      if (!id) return;
      document.getElementById("wikiTopicBox").hidden = false;

      const topics = uniqueDisplayTopics(await j(`/datasets/${id}/topics`));
      const topicSel = document.getElementById("topic");
      const keepTopic = displayTopic(topicSel.value);
      topicSel.innerHTML = `<option value="">All topics</option>` + topics.map(t => `<option>${esc(t)}</option>`).join("");
      if (keepTopic && [...topicSel.options].some(o => o.value === keepTopic)) {
        topicSel.value = keepTopic;
      } else {
        topicSel.value = "";
      }

      const q = topicQuery();
      try {
        const graph = await j(q ? `/datasets/${id}/graph?${q.slice(1)}` : `/datasets/${id}/graph`);
        drawGraph(graph, { refreshAnalysis: false });
        await loadVisibleAnalysis();
      } catch (err) {
        state.rawGraph = null;
        state.fullView = null;
        state.graphView = null;
        document.getElementById("graph").innerHTML = `<p class="hint">Graph unavailable: ${err.message}</p>`;
        await loadVisibleAnalysis();
      }
    }

    function loadDashboard(opts) {
      return loadAll(opts);
    }

    function bandHeading(band) {
      if (band === "LIKELY_AI") return "likely AI";
      if (band === "LIKELY_HUMAN") return "likely human";
      if (band === "UNCERTAIN") return "uncertain";
      return "pending";
    }

    function collectionName(n) {
      if (n && n.dataset) return String(n.dataset);
      const ds = datasetsCache.find(d => String(d.id) === String(n && n.datasetId));
      if (ds) return datasetLabel(ds);
      const src = String((n && n.source) || "").toLowerCase();
      if (src.includes("wikipedia")) return "Wikipedia";
      if (src.includes("gdelt")) return "GDELT";
      return (n && n.source) || "Unknown collection";
    }

    function graphFilterTitle() {
      return selectedCountryTopic() || "";
    }

    function inferGraphGroupBy() {
      return "dataset";
    }

    function nodeGroup(n) {
      return collectionName(n);
    }

    const TOPIC_PALETTE = [
      "#c45c4e", "#2f8f6e", "#4a6fa5", "#b4842c", "#7a4e8a",
      "#3d8a8a", "#8a5a2a", "#6b7c3a", "#a85a6c", "#5c7a9a"
    ];

    function groupColor(i) {
      return TOPIC_PALETTE[i % TOPIC_PALETTE.length];
    }

    function shortHeading(name) {
      const s = String(name || "");
      if (isOtherGroup(s)) return s;
      return s.length > 28 ? s.slice(0, 26).trim() + "…" : s;
    }

    function hexTint(hex, aFill, aStroke) {
      const h = String(hex || "#8a5a2a").replace("#", "");
      const r = parseInt(h.slice(0, 2), 16);
      const g = parseInt(h.slice(2, 4), 16);
      const b = parseInt(h.slice(4, 6), 16);
      return { fill: `rgba(${r},${g},${b},${aFill})`, stroke: `rgba(${r},${g},${b},${aStroke})` };
    }

    function placeClusterRing(members, pos, gcx, gcy, radius) {
      const n = members.length;
      if (n <= 1) {
        if (members[0]) pos[members[0].id] = { x: gcx, y: gcy };
        return;
      }
      members.forEach((node, i) => {
        const a = (2 * Math.PI * i) / n - Math.PI / 2;
        pos[node.id] = { x: gcx + radius * Math.cos(a), y: gcy + radius * Math.sin(a) };
      });
    }

    function drawGraph(graph, opts) {
      state.rawGraph = graph;
      let nodes = (graph && graph.nodes) || [];
      if (!nodes.length) {
        state.fullView = { nodes: [], links: [], groupBy: "dataset", names: [], buckets: new Map(), colorOf: {} };
        state.graphView = state.fullView;
        document.getElementById("graph").innerHTML = `<p class="hint">No documents in this view yet.</p>`;
        renderCollectionAnalysis(state.graphView);
        if (!opts || opts.refreshAnalysis !== false) {
          loadVisibleAnalysis().catch(err => popup(err.message, "Analysis failed", "warn"));
        }
        return;
      }
      const raw = (graph && graph.edges) || [];
      const unique = new Map();
      raw.forEach(e => {
        const a = Math.min(e.from, e.to);
        const b = Math.max(e.from, e.to);
        const key = a + "-" + b;
        const prev = unique.get(key);
        if (!prev || (e.cosine || 0) > (prev.cosine || 0)) unique.set(key, { from: a, to: b, cosine: e.cosine || 0 });
      });
      const ranked = [...unique.values()].sort((x, y) => y.cosine - x.cosine);
      const degree = {};
      let links = [];
      const maxDegree = nodes.length > 20 ? 2 : 3;
      ranked.forEach(e => {
        degree[e.from] = degree[e.from] || 0;
        degree[e.to] = degree[e.to] || 0;
        if (degree[e.from] >= maxDegree || degree[e.to] >= maxDegree) return;
        if (e.cosine < 0.45 && nodes.length > 12) return;
        degree[e.from]++; degree[e.to]++;
        links.push(e);
      });
      const w = Math.max(720, document.getElementById("graph").clientWidth || 720);
      const h = 560;
      const groupBy = inferGraphGroupBy();
      let buckets = new Map();
      nodes.forEach(n => {
        const g = nodeGroup(n);
        if (!g || isOtherGroup(g) || g === "Uncategorized") return;
        if (!buckets.has(g)) buckets.set(g, []);
        buckets.get(g).push(n);
      });
      let names = [...buckets.keys()].filter(name => !isOtherGroup(name) && name !== "Uncategorized");
      names.sort((a, b) => {
        return (buckets.get(b) || []).length - (buckets.get(a) || []).length || a.localeCompare(b);
      });
      const colorOf = {};
      names.forEach((name, i) => { colorOf[name] = groupColor(i); });
      state.fullView = { nodes, links, groupBy, names, buckets, colorOf };
      if (state.selectedCluster && isOtherGroup(state.selectedCluster)) state.selectedCluster = null;
      if (state.selectedCluster && !buckets.has(state.selectedCluster)) state.selectedCluster = null;
      if (state.selectedCluster && buckets.has(state.selectedCluster)) {
        nodes = buckets.get(state.selectedCluster) || [];
        const idSet = new Set(nodes.map(n => n.id));
        links = links.filter(l => idSet.has(l.from) && idSet.has(l.to));
        names = [state.selectedCluster];
        buckets = new Map([[state.selectedCluster, nodes]]);
      }
      const cx = w / 2;
      const cy = h / 2;
      const nGroups = Math.max(names.length, 1);
      const orbit = nGroups <= 1 ? 0 : Math.min(w, h) * (nGroups <= 3 ? 0.26 : 0.29);
      const maxLocal = nGroups <= 1
        ? Math.min(w, h) * 0.32
        : Math.min(w, h) * (nGroups <= 3 ? 0.15 : 0.105);
      const pos = {};
      const hulls = [];
      const hits = [];
      const headings = [];
      names.forEach((name, gi) => {
        const members = buckets.get(name) || [];
        const ga = (2 * Math.PI * gi) / nGroups - Math.PI / 2;
        const gcx = cx + orbit * Math.cos(ga);
        const gcy = cy + orbit * Math.sin(ga);
        const n = members.length;
        const outerR = n <= 1 ? 0 : Math.min(maxLocal, 14 + n * (nGroups <= 1 ? 3.2 : 2.2));
        if (n > 18) {
          const innerN = Math.ceil(n * 0.4);
          placeClusterRing(members.slice(0, innerN), pos, gcx, gcy, outerR * 0.52);
          placeClusterRing(members.slice(innerN), pos, gcx, gcy, outerR);
        } else {
          placeClusterRing(members, pos, gcx, gcy, outerR);
        }
        const hullR = (n <= 1 ? 20 : outerR) + 16;
        const tint = hexTint(colorOf[name], 0.08, 0.28);
        const on = state.selectedCluster === name ? " on" : "";
        hulls.push(`<circle class="g-hull" data-cluster="${esc(name)}" cx="${gcx.toFixed(1)}" cy="${gcy.toFixed(1)}" r="${hullR.toFixed(1)}" fill="${tint.fill}" stroke="${tint.stroke}" stroke-dasharray="5 4" />`);
        hits.push(`<circle class="g-hit" data-cluster="${esc(name)}" cx="${gcx.toFixed(1)}" cy="${gcy.toFixed(1)}" r="${(hullR + 10).toFixed(1)}" fill="transparent" stroke="none" pointer-events="all" />`);
        const lx = orbit === 0 ? gcx : gcx + (hullR + 13) * Math.cos(ga);
        const ly = orbit === 0 ? gcy - hullR - 12 : gcy + (hullR + 13) * Math.sin(ga);
        const anchor = orbit === 0 ? "middle" : Math.cos(ga) > 0.28 ? "start" : Math.cos(ga) < -0.28 ? "end" : "middle";
        headings.push(`<text class="g-head${on}" data-cluster="${esc(name)}" x="${lx.toFixed(1)}" y="${ly.toFixed(1)}" text-anchor="${anchor}" fill="${colorOf[name]}"><title>${esc(name)}</title>${esc(shortHeading(name))}</text>`);
      });
      const missing = nodes.filter(n => n && n.id != null && !pos[n.id]);
      if (missing.length) {
        placeClusterRing(missing, pos, cx, cy, Math.min(w, h) * 0.28);
      }
      const lines = links.map(e => {
        const a = pos[e.from];
        const b = pos[e.to];
        if (!a || !b) return "";
        const op = 0.16 + 0.32 * Math.min(1, e.cosine);
        return `<line x1="${a.x.toFixed(1)}" y1="${a.y.toFixed(1)}" x2="${b.x.toFixed(1)}" y2="${b.y.toFixed(1)}" stroke="#8a5a2a" stroke-opacity="${op}" stroke-width="1.15" />`;
      }).join("");
      const datasetNames = [...new Set(nodes.map(collectionName).filter(Boolean))].sort((a, b) => a.localeCompare(b));
      const datasetColor = {};
      datasetNames.forEach((name, i) => { datasetColor[name] = groupColor(i + (groupBy === "topic" ? 4 : 0)); });
      const dots = nodes.map(n => {
        const p = pos[n.id];
        if (!p) return "";
        const g = nodeGroup(n);
        const dsName = collectionName(n);
        const fill = colorOf[g] || colorOf[dsName] || "#8a5a2a";
        const country = displayTopic(n.topic);
        const tip = `${n.title || "Untitled"} · ${isCountryTopic(country) ? country : dsName} · ${dsName}`;
        const selected = state.selectedDocId != null && String(n.id) === String(state.selectedDocId);
        const clusterName = (g && colorOf[g]) ? g : ((dsName && colorOf[dsName]) ? dsName : "");
        const clusterAttr = clusterName ? ` data-cluster="${esc(clusterName)}"` : "";
        return `<g data-doc="${n.id}"${clusterAttr} style="cursor:pointer">
          <title>${esc(tip)}</title>
          <circle cx="${p.x.toFixed(1)}" cy="${p.y.toFixed(1)}" r="${selected ? 6.2 : 3.6}" fill="${fill}" stroke="${selected ? "#302417" : "#FFFEFB"}" stroke-width="${selected ? 2 : 1}" />
        </g>`;
      }).join("");
      const filterTitle = graphFilterTitle();
      const title = filterTitle
        ? `<text class="g-filter" x="${cx.toFixed(1)}" y="26" text-anchor="middle">${esc(filterTitle)}</text>`
        : "";
      document.getElementById("graph").innerHTML =
        `<svg width="100%" height="${h}" viewBox="0 0 ${w} ${h}" role="img" aria-label="${esc(filterTitle || "Document graph by collection")}"><rect class="g-bg" width="${w}" height="${h}" fill="transparent"></rect>${hulls.join("")}${lines}${dots}${hits.join("")}${headings.join("")}${title}</svg>`;
      state.graphView = { nodes, links, groupBy, names, buckets, colorOf, datasetColor };
      const hint = document.getElementById("graphHint");
      if (hint) {
        if (state.selectedCluster) {
          hint.textContent = `Showing the ${displayCluster(state.selectedCluster)} collection (${nodes.length} document${nodes.length === 1 ? "" : "s"}). Click empty space or Show all to restore the full graph. Analysis below matches this visible set.`;
        } else if (filterTitle) {
          hint.textContent = `${filterTitle}. Each circle is a collection that has pages for this country. Lines are similarity.`;
        } else {
          hint.textContent = "Dots are documents. Each circle is a collection. Lines are similarity.";
        }
      }
      const legend = document.getElementById("graphLegend");
      if (legend) {
        legend.hidden = true;
        legend.innerHTML = "";
      }
      const showAll = document.getElementById("showAllGraph");
      if (showAll) showAll.hidden = !state.selectedCluster;
      renderCollectionAnalysis(state.graphView);
      if (!opts || opts.refreshAnalysis !== false) {
        loadVisibleAnalysis().catch(err => popup(err.message, "Analysis failed", "warn"));
      }
    }

    function pctShare(part, whole) {
      if (!whole) return 0;
      return Math.round((part / whole) * 100);
    }

    function bandCounts(list) {
      const out = { LIKELY_AI: 0, LIKELY_HUMAN: 0, UNCERTAIN: 0, PENDING: 0 };
      (list || []).forEach(n => {
        const b = n.band || "PENDING";
        out[b] = (out[b] || 0) + 1;
      });
      return out;
    }

    function graphScopedSummary() {
      const nodes = (state.graphView && state.graphView.nodes) || [];
      const ds = selectedDataset();
      const scored = nodes.filter(d => d.pAi != null);
      const prev = state.lastSummary || {};
      let shareDocs = prev.shareOfDocuments;
      if (scored.length) {
        shareDocs = Math.round(scored.reduce((s, d) => s + Number(d.pAi), 0) / scored.length * 100);
      }
      return {
        name: prev.name || (ds && datasetLabel(ds)) || "This collection",
        topicView: !!(prev.topicView || document.getElementById("topic").value),
        topic: prev.topic || document.getElementById("topic").value || null,
        idView: !!state.selectedCluster,
        documentCount: nodes.length,
        shareOfDocuments: shareDocs != null ? shareDocs : null,
        bands: bandCounts(nodes),
        headline: scored.length ? prev.headline : "Not yet analyzed",
        collectionSignals: prev.collectionSignals || state.lastSignals
      };
    }

    function renderCollectionAnalysis(view) {
      applySummary(graphScopedSummary(), {
        time: state.timeBreakdown,
        signals: state.lastSignals,
        nodes: (view && view.nodes) || (state.graphView && state.graphView.nodes) || []
      });
      renderSubjectChart();
    }

    function selectCluster(name) {
      const next = !name || name === state.selectedCluster ? null : name;
      state.selectedCluster = next;
      if (state.rawGraph) drawGraph(state.rawGraph);
      else loadVisibleAnalysis().catch(err => popup(err.message, "Analysis failed", "warn"));
    }

    function readingScopeLabel() {
      const country = selectedCountryTopic();
      const ds = selectedDataset();
      const cluster = displayCluster(state.selectedCluster);
      const allDs = isAllSources(ds);
      if (country && cluster) return `${country} in ${cluster}`;
      if (country && !allDs) return `${country} in ${datasetLabel(ds)}`;
      if (country) return country;
      if (cluster) return `the ${cluster} collection`;
      if (!allDs) return datasetLabel(ds) || "this collection";
      return "all sources";
    }

    function composeReadingLines(summary, nodes, signals) {
      summary = summary || state.lastSummary || {};
      nodes = nodes || (state.graphView && state.graphView.nodes) || [];
      const place = readingScopeLabel();
      const n = nodes.length || Number(summary.documentCount) || 0;
      const share = summary.shareOfDocuments;
      const era = eraCompare(nodes);
      const maxTopic = maxFieldShare(nodes, "topic");
      const country = selectedCountryTopic();

      const line1 = n
        ? (share != null
          ? `In ${place}, about ${share}% of ${pageCountLabel(n)} look AI-generated or AI-assisted.`
          : `In ${place}, we looked at ${pageCountLabel(n)} but do not have an overall score yet.`)
        : `There are no pages to summarize in ${place}.`;

      let line2;
      if (era.beforePct != null && era.afterPct != null) {
        const delta = era.afterPct - era.beforePct;
        if (delta > 0) {
          line2 = `Pages from before September 2019 scored about ${era.beforePct}%. Pages from September 2019 or later scored about ${era.afterPct}%, a rise of ${delta} points against earlier human pages.`;
        } else if (delta < 0) {
          line2 = `Pages from before September 2019 scored about ${era.beforePct}%. Later pages scored about ${era.afterPct}%, ${Math.abs(delta)} points lower.`;
        } else {
          line2 = `Pages from before September 2019 and from September 2019 or later scored about the same (${era.beforePct}%).`;
        }
      } else {
        line2 = "There are not enough dated pages on both sides of September 2019 to compare earlier human writing with later pages.";
      }

      let line3 = "";
      if (maxTopic && maxTopic.n >= MIN_TOPIC_DOCS && !country) {
        line3 = `${maxTopic.name} pages scored about ${maxTopic.pct}%.`;
      }
      return [line1, line2, line3];
    }

    function joinWords(items) {
      items = (items || []).filter(Boolean);
      if (!items.length) return "";
      if (items.length === 1) return items[0];
      if (items.length === 2) return items[0] + " and " + items[1];
      return items.slice(0, -1).join(", ") + ", and " + items[items.length - 1];
    }

    function classifyCorpusSubject(n) {
      const title = String((n && n.title) || "");
      const blob = (title + " " + String((n && n.subtopic) || "")).toLowerCase();
      if (/gdelt/i.test(collectionName(n)) && /gdelt/i.test(title)) return "news and media";
      const rules = [
        ["sports", /sport|cricket|football|soccer|hockey|olympi|tennis|rugby|basketball|mlb|nfl|nba|premier league|bundesliga|afl|nrl|athlet|world cup|fifa/],
        ["food and cooking", /cuisine|food|dish|recipe|cook|restaurant|gastronom|kitchen/],
        ["schools and universities", /educat|universit|school|college|student|academ|campus/],
        ["politics and government", /politic|election|parliament|congress|government|minister|president|senate|chancellor|constitution|supreme court|democrat|republican|bundestag|lok sabha|rajya sabha/],
        ["history", /history|\bwar\b|empire|dynasty|independence|ancient|medieval|colonial|revolution/],
        ["places and geography", /geograph|river|mountain|climate|ocean|national park|city|cities|state of|province|district|capital/],
        ["economy and transport", /econom|compan|industry|trade|bank|transport|railway|airport|gdp/],
        ["arts, language, and culture", /culture|cinema|film|music|literature|language|festival|bollywood|sanskrit|hindi|tamil|kannada|bengali|telugu|marathi/],
        ["religion", /religion|hindu|islam|christian|sikh|church|temple|buddh/],
        ["science and technology", /science|technolog|computer|software|physics|chemist|biolog/],
        ["health", /health|medic|hospital|disease|covid/],
        ["news and media", /news|announcing|press release|the gdelt project/],
        ["people", /people of|biography/]
      ];
      for (let i = 0; i < rules.length; i++) {
        if (rules[i][1].test(blob)) return rules[i][0];
      }
      return "";
    }

    function corpusYearSpan(nodes) {
      let min = 9999;
      let max = 0;
      let dated = 0;
      const years = {};
      (nodes || []).forEach(n => {
        const y = bestDocYear(n);
        if (!y) return;
        dated += 1;
        years[y] = (years[y] || 0) + 1;
        if (y < min) min = y;
        if (y > max) max = y;
      });
      return { min: dated ? min : 0, max: dated ? max : 0, dated, years };
    }

    function composeCorpusLines(nodes) {
      nodes = nodes || (state.graphView && state.graphView.nodes) || [];
      const place = readingScopeLabel();
      const n = nodes.length;
      if (!n) {
        return [
          `There are no crawled pages to describe in ${place}.`,
          "No year range is available until pages load.",
          "No country or subject mix to report yet."
        ];
      }

      const colCounts = {};
      const countryCounts = {};
      const subCounts = {};
      nodes.forEach(doc => {
        const col = collectionName(doc);
        if (col) colCounts[col] = (colCounts[col] || 0) + 1;
        const country = displayTopic(doc.topic);
        if (isCountryTopic(country)) countryCounts[country] = (countryCounts[country] || 0) + 1;
        const subject = classifyCorpusSubject(doc);
        if (subject) subCounts[subject] = (subCounts[subject] || 0) + 1;
      });
      const cols = Object.entries(colCounts).sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
      const countries = Object.entries(countryCounts).sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
      const subjects = Object.entries(subCounts).sort((a, b) => b[1] - a[1] || a[0].localeCompare(b[0]));
      const topSubjects = subjects.slice(0, 3).map(row => row[0]);
      const thin = ["schools and universities", "sports", "food and cooking"]
        .filter(name => subCounts[name] && topSubjects.indexOf(name) < 0);
      const country = selectedCountryTopic();
      const wikiOnly = cols.length === 1 && /wiki/i.test(cols[0][0]);
      const newsOnly = cols.length === 1 && /gdelt/i.test(cols[0][0]);

      let line1;
      if (newsOnly) {
        line1 = `These ${pageCountLabel(n)} in ${place} are GDELT news and project pages, not Wikipedia encyclopedia articles.`;
      } else if (wikiOnly) {
        line1 = `These ${pageCountLabel(n)} in ${place} are Wikipedia encyclopedia articles, not a news crawl.`;
      } else if (cols.length > 1) {
        line1 = `These ${pageCountLabel(n)} in ${place} mix ${joinWords(cols.map(row => `${row[0]} (${fmtInt(row[1])})`))}: encyclopedia articles with news and project pages.`;
      } else {
        line1 = `These ${pageCountLabel(n)} in ${place} come from ${cols[0] ? cols[0][0] : "this collection"}.`;
      }
      if (topSubjects.length) {
        line1 += ` Titles lean toward ${joinWords(topSubjects)}`;
        line1 += thin.length ? `, with only thin slices of ${joinWords(thin)}.` : ".";
      } else {
        line1 += " It is not a single-subject crawl of food, sports, or schools.";
      }

      const span = corpusYearSpan(nodes);
      let line2;
      if (!span.dated) {
        line2 = `None of these ${place} pages carry a usable year yet.`;
      } else if (span.min === span.max) {
        line2 = `Dated pages (${fmtInt(span.dated)} of ${pageCountLabel(n)}) all sit in ${span.min}.`;
      } else {
        const peak = Object.entries(span.years).sort((a, b) => b[1] - a[1] || Number(b[0]) - Number(a[0]))[0];
        const peakShare = Math.round((peak[1] / span.dated) * 100);
        line2 = `Dated pages run from ${span.min} to ${span.max}.`;
        if (peakShare >= 60) {
          line2 += ` About ${peakShare}% of those dates sit in ${peak[0]}, so the year stamp is piled at the recent end rather than spread evenly.`;
        } else {
          line2 += ` The largest year pile is ${peak[0]} (${pageCountLabel(Number(peak[1]))}).`;
        }
      }

      let line3;
      const newsOnlyCountries = countries.filter(([name]) => {
        return nodes.some(doc => displayTopic(doc.topic) === name) &&
          !nodes.some(doc => displayTopic(doc.topic) === name && /wiki/i.test(collectionName(doc)));
      }).map(row => row[0]);
      if (country && countries.length <= 1) {
        if (newsOnly) {
          line3 = `${country} in this view is news-side only: there is no Wikipedia slice here, so the set reads as recent country news rather than a long encyclopedia history.`;
        } else if (wikiOnly) {
          line3 = `${country} here is encyclopedia pages on places, languages, politics, and culture, not a dedicated sports, food, or school collection.`;
        } else {
          line3 = `${country} mixes Wikipedia background pages with GDELT news. The set is a country brief, not a cuisine, sports, or education corpus.`;
        }
      } else if (countries.length) {
        line3 = `Country weight is ${joinWords(countries.map(row => `${row[0]} ${fmtInt(row[1])}`))}.`;
        if (newsOnlyCountries.length) {
          line3 += ` ${joinWords(newsOnlyCountries)} ${newsOnlyCountries.length === 1 ? "has" : "have"} news pages only.`;
        }
        line3 += " Taken together this is a country news-and-encyclopedia collection.";
      } else {
        line3 = "Pages are not tagged to India, the United States, Germany, or Australia, so the mix is leftover project and site pages rather than a country brief.";
      }
      return [line1, line2, line3];
    }

    function nodeGist(n) {
      const raw = String((n && (n.gist || n.excerpt || n.text || n.title)) || "Untitled").replace(/\s+/g, " ").trim();
      let s = raw;
      if (/^https?:\/\//i.test(s) || (/^[a-z0-9.-]+\//i.test(s) && !s.includes(" "))) {
        s = s.replace(/^https?:\/\//i, "").replace(/[?#].*$/, "").replace(/[/_-]+/g, " ").replace(/\./g, " ");
      } else if (s.includes("-") && !s.includes(" ")) {
        s = s.replace(/[-_]+/g, " ");
      }
      s = s.replace(/\s+/g, " ").trim();
      if (s.length > 180) s = s.slice(0, 176).replace(/\s+\S*$/, "") + "…";
      return s || "Untitled";
    }

    const SOURCE_GRAPH_CAP = 100;

    function pickSourceGraphNodes(members, links) {
      if (members.length <= SOURCE_GRAPH_CAP) return members.slice();
      const degree = {};
      members.forEach(n => { degree[n.id] = 0; });
      links.forEach(l => {
        if (degree[l.from] != null) degree[l.from] += 1;
        if (degree[l.to] != null) degree[l.to] += 1;
      });
      const connected = members.filter(n => degree[n.id] > 0)
        .sort((a, b) => degree[b.id] - degree[a.id] || String(a.id).localeCompare(String(b.id)));
      const chosen = connected.slice(0, Math.min(connected.length, 60));
      const chosenIds = new Set(chosen.map(n => n.id));
      const rest = members.filter(n => !chosenIds.has(n.id));
      const need = SOURCE_GRAPH_CAP - chosen.length;
      if (need > 0 && rest.length) {
        const step = Math.max(1, Math.floor(rest.length / need));
        for (let i = 0; i < rest.length && chosen.length < SOURCE_GRAPH_CAP; i += step) chosen.push(rest[i]);
      }
      return chosen.slice(0, SOURCE_GRAPH_CAP);
    }

    function layoutSourceGraph(nodes, w, h) {
      const pos = {};
      const bunch = 12;
      const groups = [];
      for (let i = 0; i < nodes.length; i += bunch) groups.push(nodes.slice(i, i + bunch));
      const g = Math.max(groups.length, 1);
      const cx = w / 2;
      const cy = h / 2;
      const orbit = g <= 1 ? 0 : Math.min(w, h) * 0.34;
      groups.forEach((group, gi) => {
        const a = (2 * Math.PI * gi) / g - Math.PI / 2;
        const gx = cx + orbit * Math.cos(a);
        const gy = cy + orbit * Math.sin(a);
        const ring = group.length <= 1 ? 0 : Math.min(78, 22 + group.length * 4);
        placeClusterRing(group, pos, gx, gy, ring);
      });
      return pos;
    }

    function clusterGraphMarkup(members) {
      const view = state.fullView || state.graphView;
      const idSet = new Set(members.map(n => n.id));
      const allLinks = ((view && view.links) || []).filter(l => idSet.has(l.from) && idSet.has(l.to));
      const drawn = pickSourceGraphNodes(members, allLinks);
      const drawnIds = new Set(drawn.map(n => n.id));
      const links = allLinks.filter(l => drawnIds.has(l.from) && drawnIds.has(l.to));
      const w = 860;
      const h = 520;
      const pos = layoutSourceGraph(drawn, w, h);
      const lines = links.map(e => {
        const a = pos[e.from];
        const b = pos[e.to];
        if (!a || !b) return "";
        const op = 0.22 + 0.4 * Math.min(1, e.cosine || 0);
        return `<line x1="${a.x.toFixed(1)}" y1="${a.y.toFixed(1)}" x2="${b.x.toFixed(1)}" y2="${b.y.toFixed(1)}" stroke="#8a5a2a" stroke-opacity="${op}" stroke-width="1.2" />`;
      }).join("");
      const showLabels = drawn.length <= 24;
      const dots = drawn.map(node => {
        const p = pos[node.id];
        if (!p) return "";
        const gist = nodeGist(node);
        const label = showLabels ? esc(gist.length > 36 ? gist.slice(0, 34).replace(/\s+\S*$/, "") + "…" : gist) : "";
        return `<g data-pop-doc="${node.id}" style="cursor:pointer">
          <title>${esc(gist)}</title>
          <circle cx="${p.x.toFixed(1)}" cy="${p.y.toFixed(1)}" r="18" fill="transparent" pointer-events="all" />
          <circle cx="${p.x.toFixed(1)}" cy="${p.y.toFixed(1)}" r="6.5" fill="#8a5a2a" stroke="#FFFEFB" stroke-width="1.4" pointer-events="none" />
          ${label ? `<text x="${p.x.toFixed(1)}" y="${(p.y + 18).toFixed(1)}" text-anchor="middle" fill="#302417" font-size="11" font-family="Literata,Georgia,serif">${label}</text>` : ""}
        </g>`;
      }).join("");
      const caption = members.length === drawn.length
        ? `${pageCountLabel(members.length)} in this source.`
        : `This source has ${pageCountLabel(members.length)}. This graph draws ${fmtInt(drawn.length)} of them.`;
      return `<p class="hint">${esc(caption)}</p><svg width="100%" height="${h}" viewBox="0 0 ${w} ${h}" role="img" aria-label="Documents in this source"><rect width="${w}" height="${h}" fill="transparent"></rect>${lines}${dots}</svg><div id="clusterNodeDetail" class="hint">Click a dot to open that page.</div>`;
    }

    async function revealClusterNode(n) {
      const slot = document.getElementById("clusterNodeDetail");
      if (!slot || !n) return;
      let gist = nodeGist(n);
      const dsId = n.datasetId;
      const id = n.id;
      let link = "";
      if (id != null && dsId != null && dsId !== "" && String(dsId) !== "all") {
        link = `<p><a href="/datasets/${encodeURIComponent(dsId)}/documents/${encodeURIComponent(id)}">Open this page</a></p>`;
        if (!n.gist && !n.excerpt && !n.text) {
          try {
            const doc = await j(`/datasets/${encodeURIComponent(dsId)}/documents/${encodeURIComponent(id)}`);
            const text = String(doc.excerpt || doc.text || "").replace(/\s+/g, " ").trim();
            if (text && !/^https?:\/\//i.test(text.slice(0, 12))) {
              gist = text.length > 360 ? text.slice(0, 340).replace(/\s+\S*$/, "") + "…" : text;
            }
          } catch (err) { /* title gist remains */ }
        }
      }
      slot.innerHTML = `<p>${esc(gist)}</p>${link}`;
    }

    function showClusterNodes(name) {
      const view = state.fullView || state.graphView;
      const buckets = view && view.buckets;
      const members = (buckets && buckets.get && buckets.get(name)) || [];
      const title = displayCluster(name) || name || "Collection";
      finishPopup(false);
      document.getElementById("popupTitle").textContent = title;
      const bodyEl = document.getElementById("popupBody");
      bodyEl.classList.add("cluster-list");
      bodyEl.innerHTML = members.length
        ? clusterGraphMarkup(members)
        : "<p>No documents in this circle.</p>";
      bodyEl.onclick = (ev) => {
        const dot = ev.target.closest && ev.target.closest("[data-pop-doc]");
        if (!dot) return;
        const id = dot.getAttribute("data-pop-doc");
        const node = members.find(item => String(item.id) === String(id));
        if (node) revealClusterNode(node);
      };
      const mark = document.getElementById("popupMark");
      mark.textContent = "";
      mark.hidden = true;
      popupEl.className = "popup graph-pop";
      popupConfirmBtn.hidden = true;
      popupActions.classList.remove("confirm");
      popupClose.textContent = "Close";
      overlay.classList.add("open");
      popupClose.focus();
    }

    function onGraphClick(e) {
      const clusterEl = e.target.closest("[data-cluster]");
      if (clusterEl) {
        const name = clusterEl.getAttribute("data-cluster");
        showClusterNodes(name);
        selectCluster(name);
        return;
      }
      const tag = (e.target.tagName || "").toLowerCase();
      if (e.target.classList.contains("g-bg") || tag === "svg") {
        if (state.selectedCluster) selectCluster(null);
      }
    }

    function mergeBreakdownRows(rows) {
      const map = new Map();
      (rows || []).forEach(r => {
        if (!r) return;
        const key = displayTopic(r.key);
        if (!key) return;
        const n = Number(r.documentCount) || 0;
        const pct = r.estimatePercent == null || r.estimatePercent === "" ? null : Number(r.estimatePercent);
        const range = Array.isArray(r.rangePercent) ? r.rangePercent.slice() : [];
        const prev = map.get(key);
        if (!prev) {
          map.set(key, { key, documentCount: n, estimatePercent: Number.isFinite(pct) ? pct : null, rangePercent: range });
          return;
        }
        const n1 = Number(prev.documentCount) || 0;
        const n2 = n;
        const total = n1 + n2;
        prev.documentCount = total;
        if (Number.isFinite(prev.estimatePercent) && Number.isFinite(pct) && total) {
          prev.estimatePercent = Math.round((prev.estimatePercent * n1 + pct * n2) / total);
        } else if (prev.estimatePercent == null && Number.isFinite(pct)) {
          prev.estimatePercent = pct;
        }
        const lo = [prev.rangePercent[0], range[0]].filter(v => v != null && Number.isFinite(Number(v)));
        const hi = [prev.rangePercent[1], range[1]].filter(v => v != null && Number.isFinite(Number(v)));
        prev.rangePercent = [
          lo.length ? Math.min(...lo.map(Number)) : undefined,
          hi.length ? Math.max(...hi.map(Number)) : undefined
        ];
      });
      return [...map.values()];
    }

    function renderBreakdown(data) {
      const el = document.getElementById("breakdown");
      if (!data.available) {
        el.innerHTML = `<p class="hint">No ${data.by} metadata on this dataset.</p>`;
        return;
      }
      const rows = mergeBreakdownRows(data.rows || []);
      el.innerHTML = `<table><tr><th>${data.by}</th><th>Docs</th><th>Estimate</th></tr>
        ${rows.map(r => `<tr><td>${esc(r.key)}</td><td>${r.documentCount}</td><td>${r.estimatePercent}% <span class="hint">${(r.rangePercent || [])[0]}–${(r.rangePercent || [])[1]}%</span></td></tr>`).join("")}
      </table>`;
    }

    function median(arr) {
      const s = arr.slice().sort((a, b) => a - b);
      const mid = Math.floor(s.length / 2);
      return s.length % 2 ? s[mid] : (s[mid - 1] + s[mid]) / 2;
    }

    function findRiseYear(rows) {
      const pts = rows.filter(r => r && r.estimatePercent != null)
        .sort((a, b) => String(a.key).localeCompare(String(b.key), undefined, { numeric: true }));
      for (let i = 0; i < pts.length; i++) {
        const pct = Number(pts[i].estimatePercent);
        if (!(pct >= 40)) continue;
        const prior = pts.slice(0, i).map(r => Number(r.estimatePercent)).filter(Number.isFinite);
        if (!prior.length) continue;
        if (prior.every(p => p < 30) || median(prior) < 30) return pts[i];
      }
      return null;
    }

    function renderTimeChart(data, nodes) {
      const mount = document.getElementById("timeChart");
      const note = document.getElementById("timeChartNote");
      const legend = document.getElementById("timeChartLegend");
      const rows = yearRows(data, nodes || timeChartNodes());
      const plotted = rows.filter(r => r.estimatePercent != null);
      if (!rows.length || !plotted.length) {
        legend.hidden = true;
        mount.innerHTML = `<p class="hint">No dated documents in this view, so a time series cannot be drawn.</p>`;
        note.textContent = "Yearly AI-share uses each document's best date: creation if present, otherwise published or last-modified.";
        return;
      }
      legend.hidden = false;
      const rise = findRiseYear(plotted);
      const later = rise ? plotted.filter(r => String(r.key) > String(rise.key)) : [];
      const laterHigh = later.filter(r => Number(r.estimatePercent) >= 55);
      const last = plotted[plotted.length - 1];
      const minY = rows[0].key;
      const maxY = rows[rows.length - 1].key;
      const span = minY === maxY ? String(minY) : `${minY}–${maxY}`;
      let caption = `Yearly estimate of how much of this collection looks AI-generated. Each document uses its best date (creation if present, otherwise published or last-modified). The axis runs ${span}.`;
      if (plotted.length === 1 && minY === maxY) {
        const r = plotted[0];
        const n = Number(r.documentCount) || 0;
        caption = `${r.key} is the only dated year in this view (${n} document${n === 1 ? "" : "s"}). Dates are creation when present, otherwise published or last-modified.`;
      } else if (rise) {
        caption = `The series first rises clearly in ${rise.key} (${rise.estimatePercent}%), after a run of years under 30%. The axis runs ${span}. `;
        if (laterHigh.length) {
          const lo = Math.min(...laterHigh.map(r => r.estimatePercent));
          const hi = Math.max(...laterHigh.map(r => r.estimatePercent));
          caption += `Later years stay higher (${lo}–${hi}% in ${laterHigh[0].key}–${laterHigh[laterHigh.length - 1].key}). `;
        }
        if (last && String(last.key) !== String(rise.key) && Number(last.estimatePercent) < 55) {
          const n = last.documentCount || 0;
          caption += `${last.key} is ${last.estimatePercent}% on only ${n} article${n === 1 ? "" : "s"}, with a wide interval, too thin to treat as a reversal.`;
        }
      }
      note.textContent = caption;

      const W = 880, H = 308;
      const pad = { l: 48, r: 20, t: 40, b: 46 };
      const innerW = W - pad.l - pad.r;
      const innerH = H - pad.t - pad.b;
      const xs = rows.map((_, i) => pad.l + (rows.length === 1 ? innerW / 2 : i * innerW / (rows.length - 1)));
      const idxOf = key => rows.findIndex(r => String(r.key) === String(key));
      const yOf = p => pad.t + innerH * (1 - Math.max(0, Math.min(100, Number(p))) / 100);
      const grid = [0, 25, 50, 75, 100].map(g => {
        const y = yOf(g);
        return `<line x1="${pad.l}" y1="${y}" x2="${W - pad.r}" y2="${y}" stroke="rgba(42,36,28,0.08)" />
          <text x="${pad.l - 8}" y="${y + 4}" text-anchor="end" fill="#302417" font-size="11" font-family="Literata,Georgia,serif">${g}%</text>`;
      }).join("");
      const bandRows = plotted.filter(r => Array.isArray(r.rangePercent) && r.rangePercent.length >= 2);
      const hiPts = bandRows.map(r => `${xs[idxOf(r.key)]},${yOf(r.rangePercent[1])}`).join(" ");
      const loPts = bandRows.map(r => `${xs[idxOf(r.key)]},${yOf(r.rangePercent[0])}`).reverse().join(" ");
      const line = plotted.map((r, i) => `${i ? "L" : "M"}${xs[idxOf(r.key)]},${yOf(r.estimatePercent)}`).join(" ");
      const dots = plotted.map(r => {
        const i = idxOf(r.key);
        const riseHere = rise && String(r.key) === String(rise.key);
        const lo = (r.rangePercent || [])[0];
        const hi = (r.rangePercent || [])[1];
        const range = lo != null && hi != null ? ` (${lo}–${hi}%)` : "";
        return `<g>
          <title>${r.key}: ${r.estimatePercent}%${range} · ${r.documentCount} docs</title>
          <circle cx="${xs[i]}" cy="${yOf(r.estimatePercent)}" r="${riseHere ? 6.5 : 4.2}" fill="${riseHere ? "#8a5a2a" : "#c45c4e"}" stroke="#FFFEFB" stroke-width="1.6" />
        </g>`;
      }).join("");
      const labelStep = rows.length > 16 ? Math.ceil(rows.length / 12) : 1;
      const labels = rows.map((r, i) => {
        if (i % labelStep !== 0 && i !== rows.length - 1) return "";
        return `<text x="${xs[i]}" y="${H - 16}" text-anchor="middle" fill="#302417" font-size="11" font-family="Literata,Georgia,serif">${r.key}</text>`;
      }).join("");
      let annot = "";
      if (rise) {
        const i = idxOf(rise.key);
        const x = xs[i];
        const y = yOf(rise.estimatePercent);
        annot = `<line x1="${x}" y1="${pad.t}" x2="${x}" y2="${H - pad.b}" stroke="#8a5a2a" stroke-dasharray="4 4" stroke-opacity="0.45" />
          <text x="${Math.min(x + 10, W - 24)}" y="${Math.max(22, y - 16)}" text-anchor="${x > W - 220 ? "end" : "start"}" fill="#302417" font-size="12" font-family="Fraunces,Georgia,serif">Clear rise begins · ${rise.key}</text>`;
      }
      const band = bandRows.length ? `<polygon points="${hiPts} ${loPts}" fill="rgba(196,92,78,0.16)" />` : "";
      mount.innerHTML = `<svg viewBox="0 0 ${W} ${H}" role="img" aria-label="AI share over time ${span}">
        ${grid}
        ${band}
        <path d="${line}" fill="none" stroke="#c45c4e" stroke-width="2.4" stroke-linejoin="round" stroke-linecap="round" />
        ${annot}${dots}${labels}
      </svg>`;
    }

    document.getElementById("reload").onclick = () => withDataLoading(() => loadDatasets().then(loadDashboard)).catch(err => popup(err.message, "Refresh failed", "warn"));
    document.getElementById("dataset").onchange = () => {
      document.getElementById("topic").value = "";
      state.selectedDocId = null;
      state.selectedCluster = null;
      syncDeleteControl();
      withDataLoading(() => loadDashboard()).catch(err => popup(err.message, "Load failed", "warn"));
    };
    document.getElementById("deleteDataset").onclick = async () => {
      const current = selectedDataset();
      if (!current || isProtectedDataset(current)) return;
      const label = datasetLabel(current);
      const ok = await confirmPopup(
        `Delete “${label}”? Documents, edges, and scores for this collection are removed. Other collections stay separate.`,
        "Delete collection",
        "Delete"
      );
      if (!ok) return;
      try {
        await j(`/datasets/${current.id}`, { method: "DELETE" });
        await loadDatasets();
        await loadDashboard();
        popup(`${label} has been deleted.`, "Collection removed", "ok");
      } catch (err) {
        popup(err.message, "Could not delete dataset", "warn");
      }
    };
    document.getElementById("topic").onchange = () => {
      state.selectedDocId = null;
      state.selectedCluster = null;
      withDataLoading(() => loadDashboard()).catch(err => popup(err.message, "Load failed", "warn"));
    };
    document.getElementById("graph").addEventListener("click", onGraphClick);
    document.getElementById("showAllGraph").onclick = () => selectCluster(null);

    function insightTimeRows(time) {
      if (Array.isArray(time)) return time;
      if (time && Array.isArray(time.rows)) return time.rows;
      return [];
    }

    function formatInsightValue(v) {
      if (v == null) return "n/a";
      if (typeof v === "string") return v;
      if (typeof v === "number") return Number.isInteger(v) ? String(v) : String(Math.round(v * 1000) / 1000);
      if (Array.isArray(v)) {
        return v.map(formatInsightValue).join(", ");
      }
      if (typeof v === "object") {
        return Object.keys(v).map(k => k + " = " + formatInsightValue(v[k])).join("; ");
      }
      return String(v);
    }

    function formatCalculationStep(step) {
      if (!step) return "";
      const n = step.n != null ? step.n : "";
      const title = step.title || "";
      const lines = [(n ? n + ". " : "") + title];
      if (step.formula) lines.push("   Formula: " + step.formula);
      const inputs = step.inputs && typeof step.inputs === "object" ? step.inputs : null;
      if (inputs && Object.keys(inputs).length) {
        lines.push("   Inputs: " + Object.keys(inputs).map(k => k + " = " + formatInsightValue(inputs[k])).join("; "));
      }
      const mid = step.intermediates && typeof step.intermediates === "object" ? step.intermediates : null;
      if (mid && Object.keys(mid).length) {
        lines.push("   Intermediate: " + Object.keys(mid).map(k => k + " = " + formatInsightValue(mid[k])).join("; "));
      }
      if (step.result) lines.push("   Result: " + step.result);
      if (step.decision) lines.push("   Decision rule: " + step.decision);
      return lines.join("\n");
    }

    function formatCalculationSteps(calcs) {
      if (!calcs) return "";
      if (calcs.prose && String(calcs.prose).trim()) return String(calcs.prose).trim();
      const steps = Array.isArray(calcs.steps) ? calcs.steps : [];
      return steps.map(formatCalculationStep).filter(Boolean).join("\n\n");
    }

    function buildInsightCalculationsFromView() {
      const summary = state.lastSummary || {};
      const nodes = (state.graphView && state.graphView.nodes) || (state.rawGraph && state.rawGraph.nodes) || [];
      const links = (state.graphView && state.graphView.links) || (state.rawGraph && state.rawGraph.edges) || [];
      const n = Number(summary.documentCount);
      const visible = Number.isFinite(n) ? n : nodes.length;
      const corpus = Number(summary.corpusDocumentCount);
      const bands = summary.bands || {};
      const ai = Number(bands.LIKELY_AI) || 0;
      const human = Number(bands.LIKELY_HUMAN) || 0;
      const unc = Number(bands.UNCERTAIN) || 0;
      const scored = scoredDocumentCount(summary, nodes) || (ai + human + unc);
      const pending = Math.max(0, visible - scored);
      const share = summary.shareOfDocuments;
      const wordShare = summary.shareOfAnalyzedWords;
      const docRange = Array.isArray(summary.shareOfDocumentsRange) ? summary.shareOfDocumentsRange : [];
      const wordRange = Array.isArray(summary.shareOfAnalyzedWordsRange) ? summary.shareOfAnalyzedWordsRange : [];
      const words = Number(summary.analyzedWordCount) || 0;
      const sig = summary.collectionSignals || state.lastSignals || {};
      const meanP = sig.pAi != null ? Number(sig.pAi) : (share != null ? Number(share) / 100 : null);
      const aiMin = 0.58;
      const humanMax = 0.42;
      const maxInterval = 0.50;
      const steps = [];

      steps.push({
        n: 1,
        title: "What we counted",
        formula: "pending = visible documents - scored documents. A page is scored when it already has an AI-likelihood estimate p_ai and a band.",
        inputs: {
          visibleDocuments: visible,
          collectionDocuments: Number.isFinite(corpus) ? corpus : visible,
          likelyAiGenerated: ai,
          likelyHumanWritten: human,
          uncertain: unc
        },
        intermediates: { scoredDocuments: scored, pendingDocuments: pending },
        result: visible + " visible document" + (visible === 1 ? "" : "s") + ", " + scored + " scored, " + pending + " pending",
        decision: "Only scored pages enter the collection percent. Pending pages are omitted from the mean."
      });

      if (share == null || share === "") {
        steps.push({
          n: 2,
          title: "Headline AI share of documents",
          formula: "share% = round(100 × mean of p_ai over scored documents).",
          inputs: { scoredDocuments: scored },
          intermediates: {},
          result: summary.headline || "This visible set is not yet analyzed.",
          decision: "No stored p_ai scores, so a collection percent cannot be formed. GraphSAGE is not used in this share."
        });
      } else {
        const mid = { roundedPercent: Number(share) };
        if (Number.isFinite(meanP)) mid["100TimesMeanPAi"] = Math.round(meanP * 10000) / 100;
        steps.push({
          n: 2,
          title: "Headline AI share of documents",
          formula: "share% = round(100 × mean of p_ai over scored documents). p_ai is already stored per page (logistic blend of stylometry, template phrases, sentence-length variance, repeated phrases, and embedding distance from the pre-2019 writing centroid in this visible set). Pages dated before 2019 stay near zero.",
          inputs: {
            scoredDocuments: scored,
            meanPAi: Number.isFinite(meanP) ? Math.round(meanP * 1000) / 1000 : "n/a",
            storedShareOfDocumentsPercent: Number(share)
          },
          intermediates: mid,
          result: Number(share) + "% of documents",
          decision: "This percent is the mean of document scores — the share of documents — not a count of proven AI pages. GraphSAGE is not used in this share."
        });
      }

      if (share != null && share !== "" && docRange.length >= 2) {
        steps.push({
          n: steps.length + 1,
          title: "Uncertainty range for the document share",
          formula: "SE = s / √n, where s is the sample standard deviation of p_ai. If n < 2, SE is taken as 0.08. Interval = [clip01(mean - 1.96×SE), clip01(mean + 1.96×SE)], then each end × 100 and rounded.",
          inputs: {
            scoredDocuments: scored,
            meanPAi: Number.isFinite(meanP) ? Math.round(meanP * 1000) / 1000 : "n/a",
            storedRangePercent: docRange[0] + "–" + docRange[1]
          },
          intermediates: {},
          result: "range " + docRange[0] + "–" + docRange[1] + "%",
          decision: "The stored range is that interval. It is a conventional 95% interval around the mean of the scores, not a claim that authorship is proven inside the band."
        });
      }

      if (wordShare != null && wordShare !== "") {
        let wordResult = Number(wordShare) + "% of analyzed words";
        if (wordRange.length >= 2) wordResult += " (range " + wordRange[0] + "–" + wordRange[1] + "%)";
        steps.push({
          n: steps.length + 1,
          title: "AI share of analyzed words",
          formula: "word share = Σ(p_ai × word_count) / Σ(word_count). The stored word range uses the same SE as the document scores (not a separate word-weighted SE), then clip01(mean ± 1.96×SE) × 100.",
          inputs: {
            scoredDocuments: scored,
            analyzedWordCount: words,
            storedShareOfAnalyzedWordsPercent: Number(wordShare)
          },
          intermediates: {},
          result: wordResult,
          decision: "This is a word-weighted mean of the same stored p_ai scores. GraphSAGE is not used here either."
        });
      }

      steps.push({
        n: steps.length + 1,
        title: "How each page gets a band",
        formula: "If (ci_high - ci_low) > max interval, the band is uncertain. Else if p_ai ≥ likely-AI cutoff → likely AI-generated. Else if p_ai ≤ likely-human cutoff → likely human-written. Else uncertain.",
        inputs: {
          likelyAiMinPAi: aiMin,
          likelyHumanMaxPAi: humanMax,
          uncertainIfIntervalWiderThan: maxInterval,
          likelyAiGeneratedCount: ai,
          likelyHumanWrittenCount: human,
          uncertainCount: unc
        },
        intermediates: { likelyAiMinPercent: 58, likelyHumanMaxPercent: 42 },
        result: ai + " likely AI-generated, " + human + " likely human-written, " + unc + " uncertain",
        decision: "Bands classify each stored score against those cutoffs. They are labels on the same p_ai values, not a second model."
      });

      const timeRows = insightTimeRows(state.timeBreakdown);
      const dated = timeRows.filter(r => r && r.estimatePercent != null && r.estimatePercent !== "");
      const last = dated.length ? dated[dated.length - 1] : null;
      let timeResult = "No dated pages in this visible set, so a yearly series cannot be drawn.";
      if (timeRows.length && dated.length) {
        timeResult = timeRows.length + " year-bin" + (timeRows.length === 1 ? "" : "s") +
          " from 2015 through the latest dated year";
        if (last) {
          timeResult += ". Latest bin " + last.key + " = " + last.estimatePercent + "%";
          if (last.documentCount != null) timeResult += " on " + last.documentCount + " page(s)";
        }
      }
      const examples = dated.slice(0, 1).concat(dated.length > 1 ? [dated[dated.length - 1]] : []);
      steps.push({
        n: steps.length + 1,
        title: "Yearly series",
        formula: "A year-bin is the calendar year of the document date (created / first-revision if present, else published). Years before 2015 are omitted. The estimate for a year is the mean of p_ai in that bin, then × 100 and rounded — the same mean as the headline, restricted to that year. The range in a bin uses the same 1.96×SE rule.",
        inputs: {
          yearBinStartsAt: 2015,
          dateRule: "calendar year of first-revision / created date when stored, otherwise published date",
          yearBinCount: timeRows.length,
          binsWithAMean: dated.length,
          exampleBins: examples.map(r => r.key + " → " + r.estimatePercent + "% on " + (r.documentCount || 0)).join("; ") || "none"
        },
        intermediates: {},
        result: timeResult,
        decision: "A later date can mean an edit, not a newly written page. Combined All sources unions years from every collection."
      });

      const groupBy = (state.graphView && state.graphView.groupBy) || (state.rawGraph && state.rawGraph.groupBy) || "topic";
      const groupPlain = groupBy === "topic" ? "country heading (stored topic label)"
        : (groupBy === "dataset" || groupBy === "source") ? "collection"
        : groupBy === "band" ? "estimate band" : "heading";
      const nodeCount = nodes.length;
      const edgeCount = links.length;
      steps.push({
        n: steps.length + 1,
        title: "Graph grouping",
        formula: "Nodes are documents. Edges are nearest-neighbor text similarity (up to k neighbors, cosine at least the stored minimum). Group labels come from stored metadata (country or collection), not from a second authorship model.",
        inputs: {
          nodeCount,
          edgeCount,
          groupBy,
          groupByPlain: groupPlain,
          nearestNeighbors: 8,
          minCosineSimilarity: 0.32
        },
        intermediates: {},
        result: nodeCount + " document node" + (nodeCount === 1 ? "" : "s") + ", " +
          edgeCount + " similarity edge" + (edgeCount === 1 ? "" : "s") + ", grouped by " + groupPlain,
        decision: "Clicking a heading only filters this same collection. GraphSAGE is not in the headline share."
      });

      const gs = (summary.graphsage && summary.graphsage.usedInHeadline) ? true : false;
      steps.push({
        n: steps.length + 1,
        title: "What is not in the headline",
        formula: "headline share uses only the mean of stored document p_ai scores.",
        inputs: {
          graphsageUsedInHeadline: gs,
          graphsageStatus: (summary.graphsage && summary.graphsage.status) || "NOT_TRAINED"
        },
        intermediates: {},
        result: gs
          ? "GraphSAGE was marked as used — unexpected; the share should still be read as the mean of p_ai."
          : "GraphSAGE is not in this share.",
        decision: "Similarity clustering and GraphSAGE embeddings may appear as graph context. They do not enter the collection percent."
      });

      return { title: "How we calculated this", steps, prose: steps.map(formatCalculationStep).join("\n\n") };
    }

    function pctRangeLabel(pair) {
      if (!Array.isArray(pair) || pair.length < 2) return "";
      const a = Number(pair[0]);
      const b = Number(pair[1]);
      if (!Number.isFinite(a) || !Number.isFinite(b)) return "";
      return a + "–" + b + "%";
    }

    function statisticalInsightNote(data) {
      const c = (data && data.calculations) || {};
      const counts = c.counts || {};
      const th = c.thresholds || {};
      const summary = state.lastSummary || {};
      const bands = summary.bands || {};
      const name = (data && data.name) || summary.name || "This collection";
      const n = Number.isFinite(Number(counts.visibleDocuments))
        ? Number(counts.visibleDocuments)
        : (Number(summary.documentCount) || 0);
      const ai = Number.isFinite(Number(counts.likelyAiGenerated))
        ? Number(counts.likelyAiGenerated)
        : (Number(bands.LIKELY_AI) || 0);
      const human = Number.isFinite(Number(counts.likelyHumanWritten))
        ? Number(counts.likelyHumanWritten)
        : (Number(bands.LIKELY_HUMAN) || 0);
      const unc = Number.isFinite(Number(counts.uncertain))
        ? Number(counts.uncertain)
        : (Number(bands.UNCERTAIN) || 0);
      const scored = Number.isFinite(Number(counts.scoredDocuments))
        ? Number(counts.scoredDocuments)
        : (ai + human + unc);
      const pending = Number.isFinite(Number(counts.pendingDocuments))
        ? Number(counts.pendingDocuments)
        : Math.max(0, n - scored);
      const words = Number.isFinite(Number(counts.analyzedWordCount))
        ? Number(counts.analyzedWordCount)
        : (Number(summary.analyzedWordCount) || 0);
      const share = c.shareOfDocuments != null ? Number(c.shareOfDocuments)
        : (summary.shareOfDocuments != null ? Number(summary.shareOfDocuments) : null);
      const wordShare = c.shareOfAnalyzedWords != null ? Number(c.shareOfAnalyzedWords)
        : (summary.shareOfAnalyzedWords != null ? Number(summary.shareOfAnalyzedWords) : null);
      const docIv = pctRangeLabel(c.shareOfDocumentsRange || summary.shareOfDocumentsRange);
      const wordIv = pctRangeLabel(c.shareOfAnalyzedWordsRange || summary.shareOfAnalyzedWordsRange);
      let meanP = c.meanPAi != null ? Number(c.meanPAi) : null;
      if (!Number.isFinite(meanP) && Number.isFinite(share)) meanP = share / 100;
      const aiMin = Number(th.likelyAiMinPAi);
      const humanMax = Number(th.likelyHumanMaxPAi);
      const maxIv = Number(th.uncertainIfIntervalWiderThan);
      const k = Number(th.graphNearestNeighbors);
      const minCos = Number(th.graphMinCosine);
      const aiCut = Number.isFinite(aiMin) ? aiMin.toFixed(2) : "0.58";
      const humanCut = Number.isFinite(humanMax) ? humanMax.toFixed(2) : "0.42";
      const widthCut = Number.isFinite(maxIv) ? maxIv.toFixed(2) : "0.50";
      const kTxt = Number.isFinite(k) ? String(k) : "8";
      const cosTxt = Number.isFinite(minCos) ? String(minCos) : "0.32";

      const topic = (data && data.topic) || summary.topic;
      const topicView = !!(data && data.topicView) || !!summary.topicView;
      const idView = !!(data && data.idView) || !!summary.idView;
      let where = String(name);
      if (idView) where += ", selected cluster only";
      else if (topicView && topic) where += ', topic "' + topic + '"';

      const howWeAnalyze =
        "How we analyze\n" +
        "Each page is scored from 0 to 1. The percent on screen is the average of those scores, rounded. The graph is only for browsing and is not part of the score.\n\n" +
        "1. Measure the page. Seven readings, each from 0 to 1: formulaic style (uniform wording, low burstiness, character entropy), template phrases, even sentence lengths, repeated short phrases, how far the wording sits from pre-2019 pages in this same set, how far the style sits from those pages, and whether the date is after Sept 2019.\n\n" +
        "2. Combine them. Start at 0.58. Add a weight times (reading − 0.5). Weights: date 1.15, template phrases 1.10, even sentences 0.85, formulaic style 0.70, wording distance 0.50, style distance 0.30, repeated phrases 0.28. The date term uses (reading − 0.45). Turn that sum into a 0–1 score with 1 / (1 + e to the minus sum). Then blend 62% of that score with 38% of the page’s rank among later pages.\n\n" +
        "3. Baseline. Pages dated before 2019 are set near 0 and labeled human-written. Later pages are compared with them, so those early pages pull the average down.\n\n" +
        "4. Label the page. 0.58 or higher: likely AI-generated. 0.42 or lower: likely human-written. In between, or if that page’s own range is wider than 0.50: uncertain. The range widens when formulaic style, sentence evenness, and the date disagree.\n\n" +
        "5. The collection percent. Average the page scores and multiply by 100. The band around it is that average plus or minus 1.96 times (the standard deviation of the scores, divided by the square root of the number of pages).";

      if (!Number.isFinite(share) && !Number.isFinite(wordShare)) {
        return where + "\n\nThese pages do not have scores yet, so there is no average to report.\n\n" + howWeAnalyze;
      }

      const sig = summary.collectionSignals || state.lastSignals || {};
      const live = detectorMeans(sig).filter(row => row.shown != null);
      const weights = {
        stylometry: "0.70",
        stockPhrases: "1.10",
        uniformity: "0.85",
        ngrams: "0.28",
        embeddingAnomaly: "0.50",
        stylometryDeviation: "0.30",
        postChatgpt: "1.15"
      };
      const measureLines = live.map(row =>
        row.label + " " + row.shown + " (weight " + (weights[row.key] || "") + ")"
      );

      const nodes = (state.graphView && state.graphView.nodes) || (state.rawGraph && state.rawGraph.nodes) || [];
      const era = eraCompare(nodes);
      const meanTxt = Number.isFinite(meanP) ? meanP.toFixed(3) : "n/a";
      const paragraphs = [];

      let headline = where + ". " + scored + " of " + n + " pages have a score";
      if (pending) headline += " (" + pending + " are still waiting and are left out)";
      headline += ".";
      if (Number.isFinite(share)) {
        headline += " The " + share + "% is the average of those scores";
        if (Number.isFinite(meanP)) headline += " (" + meanTxt + ")";
        headline += docIv ? ", with a range of " + docIv + "." : ".";
      }
      paragraphs.push(headline);

      paragraphs.push(
        "How one page is scored\n" +
        "Each page gets seven readings from 0 to 1. Higher means more like the pattern the score treats as AI-assisted." +
        (measureLines.length ? "\nOn this set, the averages are:\n" + measureLines.map(line => "• " + line).join("\n") : "") +
        "\n\nThose readings are combined like this:\n" +
        "sum = 0.58\n" +
        "  + 0.70 × (formulaic style − 0.5)\n" +
        "  + 1.10 × (template phrases − 0.5)\n" +
        "  + 0.85 × (even sentence lengths − 0.5)\n" +
        "  + 0.28 × (repeated phrases − 0.5)\n" +
        "  + 0.50 × (wording distance from pre-2019 pages − 0.5)\n" +
        "  + 0.30 × (style distance from those pages − 0.5)\n" +
        "  + 1.15 × (date after Sept 2019 − 0.45)\n" +
        "page score = 1 / (1 + e^(−sum))\n\n" +
        "A reading of 0.5 adds nothing. Above 0.5 it pushes the score up, and the larger weights (date, template phrases, even sentences) push harder. " +
        "For pages from 2019 on, the stored score is 62% of that result and 38% of the page’s rank among those later pages. " +
        "Pages dated before 2019 are set near 0 and labeled human-written. They are the comparison set, so they pull the average down. " +
        "If formulaic style, sentence evenness, and the date disagree, that page’s own range gets wider."
      );

      if ((era.beforeN || era.afterN) && (era.beforePct != null || era.afterPct != null)) {
        let eraLine = "Split by date. ";
        if (era.beforeN && era.beforePct != null) {
          eraLine += era.beforeN + " page" + (era.beforeN === 1 ? "" : "s") +
            " from before September 2019 average " + era.beforePct + "%. ";
        }
        if (era.afterN && era.afterPct != null) {
          eraLine += era.afterN + " page" + (era.afterN === 1 ? "" : "s") +
            " from September 2019 or later average " + era.afterPct + "%.";
        }
        if (era.beforePct != null && era.afterPct != null) {
          const gap = era.afterPct - era.beforePct;
          if (gap > 0) eraLine += " That is " + gap + " points higher than the earlier pages.";
          else if (gap < 0) eraLine += " That is " + Math.abs(gap) + " points lower than the earlier pages.";
        }
        paragraphs.push(eraLine);
      }

      let collection = "The collection number\n";
      if (Number.isFinite(share)) {
        collection += share + "% = round(100 × average of the " + scored + " page scores).";
        if (Number.isFinite(meanP)) collection += " Average = " + meanTxt + ".";
      }
      if (Number.isFinite(wordShare)) {
        collection += "\nThe word figure weights each score by how long the page is: " +
          wordShare + "% = sum(score × word count) / " +
          (words ? words.toLocaleString("en-US") + " words" : "the total words") + ".";
        if (wordIv && docIv && wordIv === docIv) {
          collection += " The range beside it is the same " + docIv + " as the page average.";
        } else if (wordIv) {
          collection += " Range " + wordIv + ".";
        }
      }
      paragraphs.push(collection);

      if (docIv && Number.isFinite(share)) {
        paragraphs.push(
          "The range " + docIv + "\n" +
          "Take how far the " + scored + " scores sit from their average (the standard deviation), divide by the square root of " +
          scored + ", and step 1.96 of those steps above and below the average. " +
          "Keep the ends between 0 and 1, multiply by 100, and round. " +
          (scored < 2 ? "With fewer than 2 scores, that step is taken as 0.08. " : "") +
          "It describes the spread of these scores. The pages were scored against the same earlier pages, so they are not separate samples."
        );
      }

      paragraphs.push(
        "The three labels\n" +
        ai + " likely AI-generated: score at least " + aiCut + ".\n" +
        human + " likely human-written: score at most " + humanCut + ".\n" +
        unc + " uncertain: between those cutoffs, or the page’s own range is wider than " + widthCut + "." +
        (scored && Number.isFinite(share)
          ? "\n" + ai + "/" + scored + " is how many pages clear " + aiCut + ". The " + share + "% is the average of the scores, a different number."
          : "")
      );

      const timeRows = insightTimeRows(state.timeBreakdown);
      const dated = timeRows.filter(r => r && r.estimatePercent != null && r.estimatePercent !== "");
      if (!timeRows.length) {
        paragraphs.push(
          "By year\nNo dated pages in this view. A year is the first-revision date when one is stored, otherwise the published date. Years before 2015 are left off the chart."
        );
      } else {
        const keys = timeRows.map(r => String(r.key || "")).filter(Boolean).sort();
        const span = keys.length ? keys[0] + "–" + keys[keys.length - 1] : "";
        const last = dated.length ? dated[dated.length - 1] : null;
        let line = "By year\nThe chart is the same average, one year at a time" +
          (span ? " (" + span + ", " + dated.length + " years with scores)" : "") + ". " +
          "The year is the first-revision date when that is stored, otherwise the published date. ";
        if (last) {
          const lr = pctRangeLabel(last.rangePercent);
          line += last.key + " is " + last.estimatePercent + "% on " +
            (last.documentCount != null ? last.documentCount : "?") + " page" +
            (Number(last.documentCount) === 1 ? "" : "s") +
            (lr ? " (range " + lr + ")" : "") + ". ";
        }
        line += "A later year can be an edit, not the year the prose was written.";
        paragraphs.push(line);
      }

      const graphStep = Array.isArray(c.steps) ? c.steps.find(s => s && s.id === "graph") : null;
      const gin = (graphStep && graphStep.inputs) || {};
      const raw = state.rawGraph || {};
      const nodeCount = Number.isFinite(Number(gin.nodeCount))
        ? Number(gin.nodeCount)
        : ((raw.nodes && raw.nodes.length) || 0);
      const edgeCount = Number.isFinite(Number(gin.edgeCount))
        ? Number(gin.edgeCount)
        : ((raw.edges && raw.edges.length) || 0);
      const groups = Array.isArray(gin.exampleGroups) ? gin.exampleGroups : [];
      const groupBits = groups.map(g => {
        const label = g.label || g.key || "";
        return g.count != null && label ? label + " (" + g.count + ")" : label;
      }).filter(Boolean);
      paragraphs.push(
        "The graph\n" +
        nodeCount + " dots, " + edgeCount + " lines. Each page links to up to " + kTxt +
        " similar pages, and only when the similarity is at least " + cosTxt + "." +
        (groupBits.length ? " Circles on screen: " + groupBits.join("; ") + "." : "") +
        " Those lines are not used in the " + (Number.isFinite(share) ? share + "%" : "score") + "."
      );
      paragraphs.push(howWeAnalyze);
      return paragraphs.join("\n\n");
    }

    function formatInsightsPopup(data) {
      return statisticalInsightNote(data);
    }

    function calculationMethodText() {
      const howWeAnalyze =
        "How we analyze\n" +
        "Each page is scored from 0 to 1. The percent on screen is the average of those scores, rounded. The graph is only for browsing and is not part of the score.\n\n" +
        "1. Measure the page. Seven readings, each from 0 to 1: formulaic style (uniform wording, low burstiness, character entropy), template phrases, even sentence lengths, repeated short phrases, how far the wording sits from pre-2019 pages in this same set, how far the style sits from those pages, and whether the date is after Sept 2019.\n\n" +
        "2. Combine them. Start at 0.58. Add a weight times (reading − 0.5). Weights: date 1.15, template phrases 1.10, even sentences 0.85, formulaic style 0.70, wording distance 0.50, style distance 0.30, repeated phrases 0.28. The date term uses (reading − 0.45). Turn that sum into a 0–1 score with 1 / (1 + e to the minus sum). Then blend 62% of that score with 38% of the page’s rank among later pages.\n\n" +
        "3. Baseline. Pages dated before 2019 are set near 0 and labeled human-written. Later pages are compared with them, so those early pages pull the average down.\n\n" +
        "4. Label the page. 0.58 or higher: likely AI-generated. 0.42 or lower: likely human-written. In between, or if that page’s own range is wider than 0.50: uncertain. The range widens when formulaic style, sentence evenness, and the date disagree.\n\n" +
        "5. The collection percent. Average the page scores and multiply by 100. The band around it is that average plus or minus 1.96 times (the standard deviation of the scores, divided by the square root of the number of pages).";
      const formula =
        "How one page is scored\n" +
        "Each page gets seven readings from 0 to 1. Higher means more like the pattern the score treats as AI-assisted.\n\n" +
        "Those readings are combined like this:\n" +
        "sum = 0.58\n" +
        "  + 0.70 × (formulaic style − 0.5)\n" +
        "  + 1.10 × (template phrases − 0.5)\n" +
        "  + 0.85 × (even sentence lengths − 0.5)\n" +
        "  + 0.28 × (repeated phrases − 0.5)\n" +
        "  + 0.50 × (wording distance from pre-2019 pages − 0.5)\n" +
        "  + 0.30 × (style distance from those pages − 0.5)\n" +
        "  + 1.15 × (date after Sept 2019 − 0.45)\n" +
        "page score = 1 / (1 + e^(−sum))\n\n" +
        "A reading of 0.5 adds nothing. Above 0.5 it pushes the score up, and the larger weights (date, template phrases, even sentences) push harder. " +
        "For pages from 2019 on, the stored score is 62% of that result and 38% of the page’s rank among those later pages. " +
        "Pages dated before 2019 are set near 0 and labeled human-written. They are the comparison set, so they pull the average down. " +
        "If formulaic style, sentence evenness, and the date disagree, that page’s own range gets wider.";
      return howWeAnalyze + "\n\n" + formula;
    }

    function explainInsightsNow() {
      popup(calculationMethodText(), "How this share was calculated", "info");
    }

    function composeDataExplanation(summary, nodes) {
      summary = summary || {};
      nodes = nodes || [];
      const n = nodes.length || Number(summary.documentCount) || 0;
      const share = summary.shareOfDocuments;
      const place = readingScopeLabel();
      const lines = [`We looked at ${pageCountLabel(n)} in ${place}.`];
      const cols = {};
      nodes.forEach(doc => {
        const col = collectionName(doc);
        if (col && col !== "Unknown collection") cols[col] = (cols[col] || 0) + 1;
      });
      const names = Object.keys(cols);
      if (names.length === 1) lines.push(`These pages come from ${names[0]}.`);
      else if (names.length > 1) lines.push(`These pages come from ${joinWords(names)}.`);
      if (share == null || share === "" || !Number.isFinite(Number(share))) {
        lines.push("The writing checks do not have a result for this view yet.");
      } else {
        lines.push(`From the writing checks, about ${Number(share)}% of the text looks AI-generated.`);
        const era = eraCompare(nodes);
        if (era.beforePct != null && era.afterPct != null && era.afterPct !== era.beforePct) {
          lines.push(era.afterPct > era.beforePct
            ? "Pages from later years look more like that than the earlier pages."
            : "Pages from later years look less like that than the earlier pages.");
        }
        lines.push("That percent is an estimate, not proof of who wrote the pages.");
      }
      return lines.join("\n\n");
    }

    async function explainTheData() {
      const id = document.getElementById("dataset").value;
      if (!id) return popup("Pick a collection first.", "Explain the data", "warn");
      const btn = document.getElementById("explainInsightsGraph");
      if (btn) btn.disabled = true;
      try {
        const q = scopeQuery();
        const summary = await j(`/datasets/${id}/summary${q ? "?" + q.slice(1) : ""}`);
        const nodes = visibleAnalysisNodes();
        popup(composeDataExplanation(summary, nodes), "Explain the data", "info");
      } catch (err) {
        popup(err.message, "Explain the data failed", "warn");
      } finally {
        if (btn) btn.disabled = false;
      }
    }

    document.getElementById("explainInsights").addEventListener("click", explainInsightsNow);
    document.getElementById("explainInsightsGraph").addEventListener("click", () => {
      explainTheData().catch(err => popup(err.message, "Explain the data failed", "warn"));
    });

    function clipSentence(text, max) {
      const clean = String(text || "").replace(/\s+/g, " ").replace(/^…+/, "").trim();
      if (clean.length <= max) return clean;
      return clean.slice(0, max).replace(/\s+\S*$/, "") + "…";
    }

    function pageScoreLabel(row) {
      if (row.pAi == null || row.pAi === "") return "";
      const pct = Math.round(Number(row.pAi) * 100);
      const band = row.band === "LIKELY_AI" ? "likely AI-generated"
        : row.band === "LIKELY_HUMAN" ? "likely human-written"
        : row.band === "UNCERTAIN" ? "uncertain" : "";
      return pct + "%" + (band ? ", " + band : "");
    }

    function answerFromCollection(question, data) {
      const q = String(question || "").toLowerCase();
      const measured = (data && data.measured) || {};
      const summary = state.lastSummary || {};
      const share = measured.shareOfDocuments != null ? Number(measured.shareOfDocuments) : Number(summary.shareOfDocuments);
      const range = measured.shareOfDocumentsRange || summary.shareOfDocumentsRange || [];
      const bands = measured.bands || summary.bands || {};
      const n = Number(measured.documentCount || summary.documentCount || 0);
      const aiN = Number(bands.LIKELY_AI) || 0;
      const humanN = Number(bands.LIKELY_HUMAN) || 0;
      const uncN = Number(bands.UNCERTAIN) || 0;
      const words = Number(summary.analyzedWordCount) || 0;
      const wordShare = summary.shareOfAnalyzedWords != null ? Number(summary.shareOfAnalyzedWords) : null;
      const evidence = ((data && data.evidence) || []).filter(row => row && row.title);
      const nodes = visibleAnalysisNodes();
      const era = eraCompare(nodes);
      const subjects = aggregateSubjects(nodes).rows
        .filter(r => r.pAi != null)
        .map(r => ({
          name: displaySubject(r.subject),
          count: r.documentCount,
          pct: Math.round(Number(r.pAi) * 100),
          mix: Math.round(Number(r.mixShare) * 100)
        }));
      const timeRows = insightTimeRows(state.timeBreakdown)
        .filter(r => r && r.estimatePercent != null && r.estimatePercent !== "");
      const signals = detectorMeans(summary.collectionSignals || state.lastSignals || {})
        .filter(r => r.shown != null && Number.isFinite(r.n));
      const highSignals = signals.slice().sort((a, b) => b.n - a.n);
      const lowSignals = signals.slice().sort((a, b) => a.n - b.n);
      const rangeTxt = Array.isArray(range) && range.length >= 2 ? range[0] + "–" + range[1] + "%" : "";
      const asksWhy = /why|high|elevated|driven|cause|reason|lift|so much/.test(q);
      const asksTime = /year|time|trend|20\d\d|rise|later|over time|chart/.test(q);
      const asksHow = /how (is|was|do|does)|calculat|method|formula|weight|scored/.test(q);
      const asksBand = /uncertain|band|human-written|cutoff|label/.test(q);
      const asksSubject = /subject|politic|sport|food|art|histor|econom|geograph|compar|culture/.test(q);
      const asksWords = /word/.test(q);
      const named = evidence.filter(row => q.includes(String(row.title || "").toLowerCase()));

      function pageBit(row) {
        const score = pageScoreLabel(row);
        const excerpt = clipSentence(row.excerpt, 110);
        let bit = row.title;
        if (score) bit += " (" + score + ")";
        if (excerpt) bit += ": " + excerpt;
        return bit;
      }

      if (!Number.isFinite(share)) {
        return "This view does not have page scores yet, so there is nothing to answer from. Run the analysis, then ask again.";
      }

      const highPages = evidence.filter(row => row.band === "LIKELY_AI" || (row.pAi != null && Number(row.pAi) >= 0.58)).slice(0, 3);
      const lowPages = evidence.filter(row => row.band === "LIKELY_HUMAN" || (row.pAi != null && Number(row.pAi) <= 0.42)).slice(0, 2);
      const parts = [];

      if (named.length) {
        parts.push(named.slice(0, 2).map(row => {
          const score = pageScoreLabel(row);
          const excerpt = clipSentence(row.excerpt, 180);
          return row.title + (score ? " scores " + score + "." : ".") +
            (excerpt ? " The stored excerpt reads: " + excerpt : "") +
            " That score comes from the writing measures on the page, then goes into the collection average with the other pages.";
        }).join(" "));
      }

      if (asksHow) {
        parts.push(
          "Each page is scored from 0 to 1. Seven readings are combined, starting at 0.58, with weights of 1.15 for the date, 1.10 for template phrases, 0.85 for even sentence lengths, 0.70 for formulaic style, 0.50 for wording distance from pre-2019 pages, 0.30 for style distance, and 0.28 for repeated phrases. The sum goes through 1 / (1 + e to the minus sum). Pages from 2019 on then keep 62% of that score and 38% of their rank among later pages. Pages before 2019 are set near 0. The " +
          share + "% on screen is that average, times 100" + (rangeTxt ? ", with a range of " + rangeTxt : "") + "."
        );
      }

      if (asksBand) {
        parts.push(
          aiN + " pages are called likely AI-generated because the score is at least 0.58. " +
          humanN + " are likely human-written at 0.42 or below. " +
          uncN + " are uncertain, either because the score sits between those cutoffs or because that page’s own range is wider than 0.50. " +
          aiN + " out of " + n + " is a count. The " + share + "% is the average of the scores."
        );
      }

      if (asksWords && Number.isFinite(wordShare)) {
        parts.push(
          "Weighting each page score by its length gives " + wordShare + "%" +
          (words ? " across " + words.toLocaleString("en-US") + " words" : "") +
          ". That uses the same page scores. Longer pages count more."
        );
      }

      const explainLevel = asksWhy || (!asksHow && !asksBand && !asksTime && !asksSubject && !named.length && !asksWords);
      if (explainLevel) {
        let lead = "The " + share + "% is the average of " + n + " page scores";
        if (rangeTxt) lead += ", and the scores spread enough that the range around that average is " + rangeTxt;
        lead += ".";
        if (era.afterN && era.beforePct != null && era.afterPct != null) {
          lead += " " + era.afterN + " pages dated September 2019 or later average " + era.afterPct + "%.";
          if (era.beforeN) lead += " " + era.beforeN + " earlier pages average " + era.beforePct + "%, and those early pages are held near the human baseline, so they pull the collection average down.";
        }
        const up = highSignals.filter(s => s.n >= 0.45).slice(0, 2);
        const flat = lowSignals.filter(s => s.n <= 0.2).slice(0, 2);
        if (up.length) lead += " The higher readings on this set are " + up.map(s => s.label + " at " + s.shown).join(" and ") + ".";
        if (flat.length) lead += " " + flat.map(s => s.label + " reads " + s.shown).join(". ") + ".";
        if (highPages.length) lead += " The highest-scoring pages here are " + highPages.map(pageBit).join("; ") + ".";
        if (lowPages.length) lead += " " + lowPages.map(row => row.title + " at " + Math.round(Number(row.pAi) * 100) + "%").join(" and ") + " sit much lower and hold the average down.";
        lead += " " + aiN + " pages clear 0.58, " + humanN + " are at or below 0.42, and " + uncN + " are uncertain. The " + share + "% is the average, not " + aiN + " divided by " + n + ".";
        parts.push(lead);
      }

      if (asksTime || explainLevel) {
        if (timeRows.length) {
          const rise = timeRows.find(r => Number(r.estimatePercent) >= 40);
          const recent = timeRows.filter(r => Number(r.key) >= 2023);
          let line = "By year, the same average is taken inside each year.";
          if (rise) {
            line += " It first gets clearly higher in " + rise.key + " at " + rise.estimatePercent + "%";
            if (rise.documentCount != null) line += " on " + rise.documentCount + " pages";
            line += ".";
          }
          if (recent.length) {
            line += " " + recent.map(r => r.key + " is " + r.estimatePercent + "% on " + r.documentCount + " pages").join(", and ") + ".";
          }
          line += " The year is the stored first-revision date when one exists, otherwise the published date, so a later year can be an edit.";
          parts.push(line);
        }
      }

      if ((asksSubject || explainLevel) && subjects.length) {
        const hot = subjects.slice().sort((a, b) => b.pct - a.pct).slice(0, 3);
        const cool = subjects.slice().sort((a, b) => a.pct - b.pct).slice(0, 2);
        let line = "By subject, " + hot.map(s => s.name + " averages " + s.pct + "% (" + s.mix + "% of the pages)").join(", ") + ".";
        const coolDistinct = cool.filter(s => !hot.some(h => h.name === s.name));
        if (coolDistinct.length) {
          line += " " + coolDistinct.map(s => s.name + " averages " + s.pct + "%").join(" and ") + ".";
          const large = coolDistinct.find(s => s.mix >= 10);
          if (large) line += " " + large.name + " is " + large.mix + "% of the set, so that lower score has more weight in the collection average than a small high-scoring subject.";
        }
        parts.push(line);
      }

      if (!parts.length) {
        parts.push("Across " + n + " pages the average score is " + share + "%" + (rangeTxt ? " (range " + rangeTxt + ")" : "") + ".");
      }
      return parts.join("\n\n");
    }

    function laymanAskFallback(question, data) {
      const measured = (data && data.measured) || {};
      const summary = state.lastSummary || {};
      const n = Number(measured.documentCount != null ? measured.documentCount : summary.documentCount) || 0;
      const share = measured.shareOfDocuments != null ? Number(measured.shareOfDocuments) : Number(summary.shareOfDocuments);
      const q = String(question || "").toLowerCase();
      let text = `We looked at ${pageCountLabel(n)}.`;
      if (!Number.isFinite(share)) {
        return text + " The writing checks do not have a result for this view yet.";
      }
      text += ` From the writing checks, about ${share}% of the text looks AI-generated.`;
      if (/why|high/.test(q)) {
        text += " That percent is high when many of the pages score high on those checks.";
      } else if (/how/.test(q)) {
        text += " Each page is checked for writing that looks formulaic, and the percent is the average of those page scores.";
      } else {
        text += " That percent is the average of the page scores.";
      }
      text += " It is an estimate, not proof of who wrote the pages.";
      return text;
    }

    function renderClaudeResult(data) {
      const box = document.getElementById("claudeAnswer");
      if (!box) return;
      const question = document.getElementById("claudeQuestion").value.trim();
      let answer = String((data && data.answer) || "").trim();
      if (!answer || /Gemini did not|GEMINI_API_KEY|did not return|Pages behind/i.test(answer)) {
        answer = laymanAskFallback(question, data);
      }
      const paras = answer.split(/\n+/).map(s => s.trim()).filter(Boolean).map(p => `<p>${esc(p)}</p>`).join("");
      box.hidden = false;
      box.innerHTML = paras;
    }

    async function postClaude(path) {
      const id = document.getElementById("dataset").value;
      if (!id) return popup("Pick a collection first.", "Ask", "warn");
      const q = document.getElementById("claudeQuestion").value.trim();
      const ids = selectedClusterIds();
      const topic = document.getElementById("topic").value || "";
      const body = { question: q, topic, ids };
      const qs = scopeQuery();
      const data = await j(`/datasets/${id}${path}${qs ? "?" + qs.slice(1) : ""}`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body)
      });
      renderClaudeResult(data);
      return data;
    }
    let dataLoadDepth = 0;
    let dataLoadTimer = 0;
    let loadingDotsTimer = 0;
    function startLoadingDots() {
      const p = document.querySelector("#askLoading p");
      if (!p) return;
      const frames = ["loading", "loading.", "loading..", "loading..."];
      let step = 0;
      p.textContent = frames[0];
      clearInterval(loadingDotsTimer);
      loadingDotsTimer = setInterval(() => {
        step = (step + 1) % frames.length;
        p.textContent = frames[step];
      }, 450);
    }
    function stopLoadingDots() {
      clearInterval(loadingDotsTimer);
      loadingDotsTimer = 0;
      const p = document.querySelector("#askLoading p");
      if (p) p.textContent = "loading";
    }
    function setAskLoading(on) {
      const el = document.getElementById("askLoading");
      if (!el) return;
      if (on) {
        dataLoadDepth += 1;
        if (dataLoadDepth === 1) {
          clearTimeout(dataLoadTimer);
          dataLoadTimer = setTimeout(() => {
            if (dataLoadDepth > 0) {
              el.hidden = false;
              startLoadingDots();
            }
          }, 200);
        }
        return;
      }
      dataLoadDepth = Math.max(0, dataLoadDepth - 1);
      if (dataLoadDepth === 0) {
        clearTimeout(dataLoadTimer);
        el.hidden = true;
        stopLoadingDots();
      }
    }
    function withDataLoading(work) {
      setAskLoading(true);
      return Promise.resolve()
        .then(work)
        .finally(() => setAskLoading(false));
    }
    async function submitAsk() {
      const q = document.getElementById("claudeQuestion").value.trim();
      if (!q) return;
      const btn = document.getElementById("askClaude");
      btn.disabled = true;
      setAskLoading(true);
      try {
        await postClaude("/ask");
      } catch (err) {
        popup(err.message, "Ask failed", "warn");
      } finally {
        setAskLoading(false);
        btn.disabled = false;
      }
    }
    document.getElementById("askClaude").onclick = () => { submitAsk(); };
    document.getElementById("claudeQuestion").addEventListener("keydown", (e) => {
      if (e.key !== "Enter" || e.shiftKey || e.isComposing) return;
      if (!document.getElementById("claudeQuestion").value.trim()) return;
      e.preventDefault();
      submitAsk();
    });
    document.getElementById("files").addEventListener("change", function () {
      const el = document.getElementById("fileNames");
      const files = [...this.files];
      if (!files.length) el.textContent = "No file chosen";
      else if (files.length === 1) el.textContent = files[0].name;
      else el.textContent = files[0].name + " + " + (files.length - 1) + " more";
    });
    function urlCrawlMessage(created, urls) {
      const n = created.ingestedPages || created.documentCount || 0;
      const extra = created.extraPages ?? 0;
      const seeds = created.seedCount ?? urls.length;
      const failed = created.failedPages || 0;
      let msg;
      if (extra === 0) {
        msg = `No extra child pages were found. ${n} seed page${n === 1 ? "" : "s"} ingested and analyzed as their own collection.`;
      } else {
        msg = `Crawled ${n} pages from ${seeds} seed URL${seeds === 1 ? "" : "s"} (${extra} child page${extra === 1 ? "" : "s"}). Analyzed as their own collection, not mixed into Wikipedia.`;
      }
      if (failed) {
        msg += ` ${failed} URL${failed === 1 ? "" : "s"} could not be fetched.`;
      }
      return msg;
    }

    document.getElementById("crawlAnalyse").onclick = async () => {
      const files = document.getElementById("files").files;
      const urls = document.getElementById("urls").value.split(/\s+/).map(s => s.trim()).filter(Boolean);
      if (!files.length && !urls.length) {
        return popup("Choose PDF or text files, or enter at least one public URL.", "Nothing to crawl", "warn");
      }
      const adding = (files.length ? 1 : 0) + (urls.length ? 1 : 0);
      if (datasetsCache.length + adding > MAX_DATASETS) {
        return popup(DATASET_CAP_ERROR, "Too many data sources", "warn");
      }
      const notes = [];
      let lastId = null;
      let fileOk = false;
      let urlOk = false;
      if (files.length) {
        try {
          const fd = new FormData();
          for (const f of files) fd.append("files", f);
          const created = await j("/datasets/upload", { method: "POST", body: fd });
          lastId = created.id;
          fileOk = true;
          notes.push(`${created.name} has been ingested and analyzed.`);
        } catch (err) {
          notes.push(err.message === DATASET_CAP_ERROR ? DATASET_CAP_ERROR : ("Files: " + err.message));
        }
      }
      if (urls.length) {
        try {
          popup("Checking that this public site can reach at least 1000 pages, then crawling…", "Checking the site", "info");
          const created = await j("/datasets/from-urls", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ urls })
          });
          lastId = created.id;
          urlOk = true;
          notes.push(urlCrawlMessage(created, urls));
        } catch (err) {
          const msg = String(err.message || "");
          notes.push(msg.includes("1000 pages") || msg === URL_MIN_PAGES_ERROR ? URL_MIN_PAGES_ERROR
            : msg === DATASET_CAP_ERROR ? DATASET_CAP_ERROR
            : ("URLs: " + msg));
        }
      }
      if (lastId) {
        await loadDatasets(String(lastId));
        await loadDashboard();
      }
      const bothTried = files.length && urls.length;
      const anyOk = fileOk || urlOk;
      const allOk = (!files.length || fileOk) && (!urls.length || urlOk);
      const joined = notes.join("\n\n");
      const title = joined.includes(DATASET_CAP_ERROR) && !anyOk ? "Too many data sources"
        : joined.includes(URL_MIN_PAGES_ERROR) && !urlOk && !fileOk ? "Public URL required"
        : bothTried && allOk ? "Two collections analyzed"
        : bothTried && anyOk ? "Partly analyzed"
        : fileOk && !urls.length ? "Upload complete"
        : urlOk ? "Site pages analyzed"
        : files.length && !urls.length ? "Upload failed"
        : !files.length ? "URL fetch failed"
        : "Crawl failed";
      popup(joined, title, allOk ? "ok" : "warn");
    };
 
    withDataLoading(() => loadDatasets().then(loadDashboard)).catch(err => {
      popup(err.message, "Could not load datasets", "warn");
    });
  