// Audit-tool controls against the retained UNPATCHED nine-PDF geometry corpus.
// Does not generate/edit PDFs or launch WebEngine. Temporary JSON/symlinks only.
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');
if (process.argv.length !== 5)
  throw Error('Usage: node audit-tool-regression.cjs PYTHON UNPATCHED_GEOMETRY_DIRECTORY NEW_JSON_REPORT');
const python = process.argv[2];
const corpus = fs.realpathSync(process.argv[3]);
const report = path.resolve(process.argv[4]);
if (fs.existsSync(report)) throw Error('Refusing to replace an existing report');
const audit = path.join(__dirname, 'audit-anchor-geometry.py');
const manifestPath = path.join(corpus, 'anchor-geometry-results.json');
const original = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
if (original.records.length !== 9) throw Error('Complete unpatched corpus required');
const hash = p => crypto.createHash('sha256').update(fs.readFileSync(p)).digest('hex');
const inputPaths = [manifestPath, ...original.records.map(r => {
  if (r.file !== r.name + '.pdf' || path.basename(r.file) !== r.file)
    throw Error('Unexpected corpus path');
  return path.join(corpus, r.file);
})];
const before = inputPaths.map(p => ({path: p, sha256: hash(p)}));
const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'wiz-pdf-audit-controls-'));
const checks = [];
function check(name, ok) {
  checks.push({name, passed: !!ok});
  console.log((ok ? 'PASS ' : 'FAIL ') + name);
  if (!ok) throw Error(name);
}
function run(name, mutate, options = [], missingPdf = false, existingReport = false) {
  const dir = path.join(temp, name);
  fs.mkdirSync(dir);
  const manifest = structuredClone(original);
  mutate(manifest);
  fs.writeFileSync(path.join(dir, 'anchor-geometry-results.json'), JSON.stringify(manifest), {flag:'wx'});
  for (const [i, r] of original.records.entries()) {
    if (missingPdf && i === 0) continue;
    fs.symlinkSync(path.join(corpus, r.file), path.join(dir, r.file));
  }
  const out = path.join(dir, 'audit.json');
  if (existingReport) fs.writeFileSync(out, 'KEEP-EXISTING-REPORT', {flag:'wx'});
  const run = spawnSync(python, [audit, dir, '--require-accurate', '--output', out, ...options],
                        {encoding:'utf8', timeout:30000, maxBuffer:1024*1024});
  if (run.error || run.signal) throw run.error || Error('Audit terminated: ' + run.signal);
  return {status:run.status, output:out, stdout:run.stdout, stderr:run.stderr,
          result:fs.existsSync(out) && !existingReport ? JSON.parse(fs.readFileSync(out,'utf8')) : null};
}
let failure = null;
try {
  const full = run('full-original', () => {});
  check('complete unpatched corpus fails full acceptance with seven bad cases',
        full.status === 1 && full.result.casesWithPositionError === 7 && !full.result.engineAccepted);
  check('original source click rectangles remain contained and aligned',
        full.result.allSourceLinksContained && full.result.allSourceLinksAligned);
  const subset = run('accurate-subset', () => {}, ['--cases','a4-zero-narrow','a4-css20-api0']);
  check('two accurate controls pass positional checks without accepting an engine',
        subset.status === 0 && subset.result.acceptancePassed && subset.result.subsetOnly
        && !subset.result.engineAccepted && subset.result.records.length === 2);
  const invalid = [
    ['empty', m => {m.records=[];}],
    ['missing-case', m => {m.records.pop();}],
    ['duplicate-case', m => {m.records[8]=structuredClone(m.records[0]);}],
    ['unsaved', m => {m.records[0].saved=false;}],
    ['changed-html', m => {m.records[0].htmlSame=false;}],
    ['invalid-layout', m => {m.records[0].layoutValid=false;}],
    ['unexpected-file', m => {m.records[0].file='../unexpected.pdf';}],
    ['generation-failed', m => {m.failures=1;}],
    ['persistent-profile', m => {m.offTheRecord=false;}],
    ['registered-script', m => {m.registeredScripts=1;}],
    ['remote-request', m => {m.blockedRequests=1;}],
  ];
  for (const [name, mutate] of invalid) {
    const bad = run(name, mutate);
    check(name + ' cannot create an acceptance report', bad.status !== 0 && !fs.existsSync(bad.output));
  }
  const unknown = run('unknown-subset', () => {}, ['--cases','unknown']);
  check('unknown subset is rejected', unknown.status !== 0 && !fs.existsSync(unknown.output));
  const missing = run('missing-pdf', () => {}, [], true);
  check('missing PDF cannot create an acceptance report', missing.status !== 0 && !fs.existsSync(missing.output));
  const existing = run('existing-report', () => {}, ['--cases','a4-zero-narrow'], false, true);
  check('existing audit report is not replaced', existing.status !== 0
        && fs.readFileSync(existing.output,'utf8') === 'KEEP-EXISTING-REPORT');
  check('all nine PDFs and the input manifest are byte-identical after every control',
        before.every(f => hash(f.path) === f.sha256));
} catch (error) {
  failure = String(error.stack || error);
} finally {
  // Only the uniquely-created temporary fixture tree, containing JSON and
  // symlinks, is removed. fs.rm does not follow symlinks to the source corpus.
  fs.rmSync(temp, {recursive:true, force:false});
}
fs.writeFileSync(report, JSON.stringify({readOnlyPdfInputs:true,generatedPdfs:0,
  checks,passed:checks.filter(c=>c.passed).length,failures:checks.filter(c=>!c.passed).length,
  error:failure,inputs:before}, null, 2)+'\n', {flag:'wx'});
if (failure) {console.error(failure); process.exitCode=1;}
