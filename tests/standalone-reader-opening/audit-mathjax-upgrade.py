"""Read-only acceptance of four fixed synthetic component PDFs, not arbitrary notes."""
import argparse
import json
import statistics
from pathlib import Path
from urllib.parse import unquote
import pdfplumber
from pypdf import PdfReader

p = argparse.ArgumentParser()
p.add_argument("directory", type=Path)
p.add_argument("--output", required=True, type=Path)
a = p.parse_args()
records, failures = [], []
expected = {"mjx-eqn-component:equation", "mjx-eqn-component:align", "mjx-eqn-component:multline", "wiz-math-component:theorem"}
for version in ("old", "new"):
    state = json.loads((a.directory / (version + "-runtime") / "runtime.json").read_text())
    if state["failures"] or state["blockedExternalRequests"] or not state["sourceIntact"]:
        failures.append(version + ": runtime failed")
    for mode in ("a4", "reading"):
        path = a.directory / (version + "-runtime") / (mode + ".pdf")
        r = PdfReader(path)
        normalize = lambda s: unquote(str(s).removeprefix("/"))
        dests = {normalize(k): v for k, v in r.named_destinations.items()}
        gotos, remote_files = [], []
        for page in r.pages:
            for ref in page.get("/Annots", []):
                x = ref.get_object()
                action = x.get("/A", {})
                action = action.get_object() if hasattr(action, "get_object") else action
                target = action.get("/D") if action.get("/S") == "/GoTo" else x.get("/Dest")
                if target is not None:
                    gotos.append(normalize(target))
                if action.get("/S") == "/URI" and str(action.get("/URI", "")).startswith("file:"):
                    remote_files.append(str(action["/URI"]))
        if set(gotos) != expected or len(gotos) != 4 or any(k not in dests for k in gotos) or remote_files:
            failures.append(version + "/" + mode + ": unresolved or unexpected links")
        width, height = float(r.pages[0].mediabox.width), float(r.pages[0].mediabox.height)
        expected_width = 210 * 72 / 25.4 if mode == "a4" else 1100 * .75 + 20 * 72 / 25.4
        if abs(width - expected_width) > 1 or abs(height - 297 * 72 / 25.4) > 1:
            failures.append(version + "/" + mode + ": wrong page geometry")
        with pdfplumber.open(path) as pdf:
            text = "\n".join(pg.extract_text() or "" for pg in pdf.pages)
            compact = "".join(text.split())
            for value in ("Synthetic component formulas", "Reference", r"\begin{equation}code_only\end{equation}"):
                if "".join(value.split()) not in compact:
                    failures.append(version + "/" + mode + ": missing ordinary or code text")
            words = [hit for pg in pdf.pages for hit in pg.search("Reference", regex=False)]
            font = statistics.median(c["size"] for c in words[0]["chars"]) if len(words) == 1 else 0
            if abs(font - 12) > .02:
                failures.append(version + "/" + mode + ": ordinary text shrunk")
            page_text_lengths = [len(pg.extract_text() or "") for pg in pdf.pages]
        targets = {k: {"page": r.get_destination_page_number(dests[k]), "top": float(dests[k]["/Top"])} for k in expected}
        records.append({"version": version, "mode": mode, "pages": len(r.pages), "widthPt": width, "heightPt": height, "fontPt": font, "gotos": sorted(gotos), "targets": targets, "pageTextLengths": page_text_lengths})
for mode in ("a4", "reading"):
    old, new = [next(r for r in records if r["version"] == v and r["mode"] == mode) for v in ("old", "new")]
    if old["pages"] != new["pages"] or old["pageTextLengths"] != new["pageTextLengths"]:
        failures.append(mode + ": component update changed fixture pagination or text")
    for key in expected:
        if old["targets"][key]["page"] != new["targets"][key]["page"] or abs(old["targets"][key]["top"] - new["targets"][key]["top"]) > 1:
            failures.append(mode + ": destination moved by more than 1pt: " + key)
relative_failures = []
for mode in ("a4", "reading"):
    old, new = [next(r for r in records if r["version"] == v and r["mode"] == mode) for v in ("old", "new")]
    if abs(old["fontPt"] - new["fontPt"]) > .02 or old["gotos"] != new["gotos"] or old["pageTextLengths"] != new["pageTextLengths"]:
        relative_failures.append(mode + ": old/new component rendering differs")
result = {"accepted": not failures, "failures": failures, "componentRelativeComparisonPassed": not relative_failures, "relativeFailures": relative_failures, "records": records, "scope": "four fixed local component PDFs; not full renderer or real-client acceptance"}
with a.output.open("x") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
    stream.write("\n")
print(json.dumps({"accepted": not failures, "pdfs": len(records), "failures": failures}))
raise SystemExit(bool(failures))
