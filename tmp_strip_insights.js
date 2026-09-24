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
    .replace(/\s*GraphSAGE is not in this share\./gi, "")
    .replace(/\s*GraphSAGE is not in the headline share\./gi, "")
    .replace(/\s*No medical judgment is made\./gi, "")
    .replace(/\s*No medical judgment\./gi, "")
    .replace(/\s+/g, " ")
    .replace(/\s+\./g, ".")
    .trim();
}

const agentClose =
  "These figures are an ESTIMATE from local stylometry and detectors, " +
  "not proof of authorship. GraphSAGE is not in this share. " +
  "No medical judgment is made.";

const cppFallbackTail =
  "Pages before 2019 stay near zero. GraphSAGE is not in this share.";

const cppException =
  "These figures are an ESTIMATE, not proof of authorship. " +
  "GraphSAGE is not in this share. No medical judgment is made.";

const fullAgent = [
  "All sources / 6669 visible documents.",
  "Estimated AI-generated share: 12% of documents and 10% of analyzed words.",
  "Bands: 1 likely AI-generated, 2 likely human-written, 3 uncertain.",
  "The yearly series covers 8 year-bins (2015-2026).",
  "The graph groups 10 documents by heading.",
  agentClose,
].join("\n\n");

for (const [name, text] of [
  ["agentClose", agentClose],
  ["cppFallbackTail", cppFallbackTail],
  ["cppException", cppException],
  ["fullAgent", fullAgent],
]) {
  const out = stripProofDisclaimer(text);
  const lead = (out.match(/^[^.]+(?:\.)?/) || [out])[0];
  console.log("====", name);
  console.log("LEAD:", lead);
  console.log("FULL:", out);
  console.log("HAS_GRAPHSAGE:", /graphsage/i.test(out));
  console.log("HAS_MEDICAL:", /medical judgment/i.test(out));
}
