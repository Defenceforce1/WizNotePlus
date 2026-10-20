// Read-only evidence audit. Prints a new machine baseline to stdout; does not
// modify a report, source, note, installed application, or native preference.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const assert = require('assert/strict');
const project = path.resolve(process.argv[2] || '.');
const repo = path.join(project, 'work/WizNotePlus');
const output = path.join(project, 'outputs/exploration-20261020/phase114');
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const read = name => JSON.parse(fs.readFileSync(path.join(output, name)));
const previous = JSON.parse(fs.readFileSync(path.join(project,
  'outputs/baselines/development-20261020-phase113.json')));
const unchanged = {};
for (const [name, expected] of Object.entries({...previous.unchanged, ...previous.sourceHashes})) {
  if (name.startsWith('docs/')) continue; // Current scope/report updates are explicit below.
  const actual = hash(path.join(repo, name));
  assert.equal(actual, expected, `Existing code changed: ${name}`);
  unchanged[name] = actual;
}
const developerBinary = 'tmp/main-build-20261017/bin/WizNotePlus.app/Contents/MacOS/WizNotePlus';
assert.equal(hash(path.join(project, developerBinary)), previous.artifactHashes[developerBinary]);
assert.match(fs.readFileSync(path.join(project, 'tmp/main-build-20261017/CMakeCache.txt'), 'utf8'),
  /^WIZNOTE_ENABLE_MARKDOWN_RECOVERY:BOOL=OFF$/m);
const main = ['main-window.json', 'main-window-repeat.json'].map(name => {
  const r = read(name);
  assert.equal(r.failures, 0);
  assert.equal(r.exitCode, 0);
  assert.equal(r.completedSteps, r.totalSteps);
  assert.equal(r.namedChecks, 170);
  assert.equal(r.appActiveAtEnd, true);
  assert.equal(r.nativeControlsAcceptanceEligible, true);
  const log = fs.readFileSync(path.join(output, name.replace('.json', '.log')), 'utf8');
  assert.equal((log.match(/^PASS /gm) || []).length, r.namedChecks);
  assert.doesNotMatch(log, /^FAIL /m);
  return {file: name, checks: r.namedChecks, steps: r.completedSteps, failures: 0};
});
const headers = ['header-current.json', 'header-system.json'].map(name => {
  const r = read(name);
  assert.equal(r.normalExit, true);
  assert.equal(r.sourceSha256, hash(path.join(repo, 'src/mac/WizMacHelper.mm')));
  // The sampler also observes orderly post-quit hiding (visible=false,
  // geometry=0). Those raw samples remain in the original reports. They are
  // not clickable-window states, and never evidence for a visible hit failure.
  const visible = r.samples.filter(s => s.visible && !s.minimized && !s.fullscreen);
  assert.ok(visible.length > 0);
  for (const sample of visible) {
    assert.equal(sample.activeSharing, false);
    assert.equal(sample.controls.length, 3);
    for (const c of sample.controls) {
      assert.ok(c.legacyHitMatches && c.enabled && !c.hidden);
      if (r.mode === 'current') assert.ok(Math.abs(c.centerY - sample.searchY) <= 2);
    }
  }
  return {file: name, visibleSamples: visible.length,
    postHideSamplesExcluded: r.samples.length - visible.length, unsharedVisibleHitsPassed: true};
});
const spelling = fs.readFileSync(path.join(output, 'spellcheck-cocoa.log'), 'utf8');
assert.match(spelling, /Spelling checks: 34 failures: 0/);
assert.doesNotMatch(spelling, /^FAIL /m);
const prompt = read('native-ui-verified.json');
assert.equal(prompt.checks, 48);
assert.equal(prompt.failures, 0);
assert.equal(prompt.normalExit, true);
assert.equal(prompt.manualSaveSelectionTested, false);
assert.equal(prompt.platform, 'cocoa');
assert.equal(prompt.results.length, prompt.checks);
assert.ok(prompt.results.every(r => r.passed === true));
assert.equal(prompt.promptSourceSha256, hash(path.join(repo,
  'src/widgets/WizMarkdownExportRecoveryPrompt.cpp')));
const manual = fs.existsSync(path.join(output, 'native-ui-manual.json'))
  ? read('native-ui-manual.json') : null;
const manualAccepted = !!manual && manual.normalExit && manual.failures === 0
  && manual.manualSaveSelectionTested === true;
if (manualAccepted) {
  assert.equal(manual.checks, 54);
  assert.equal(manual.platform, 'cocoa');
  assert.equal(manual.promptSourceSha256, prompt.promptSourceSha256);
  assert.equal(manual.results.length, manual.checks);
  assert.ok(manual.results.every(r => r.passed === true));
  const log = fs.readFileSync(path.join(output, 'native-ui-manual.log'), 'utf8');
  assert.doesNotMatch(log, /^FAIL /m);
  assert.match(log, /Native UI checks: 54 failures: 0/);
}
const changedOrNew = {};
for (const name of ['docs/next-build-requests.md', 'docs/exploration-2026-10-20-phase114.md',
  'tests/standalone-native-ui/CMakeLists.txt', 'tests/standalone-native-ui/main.mm',
  'tests/standalone-native-ui/README.md', 'tests/standalone-native-ui/audit-phase114.cjs'])
  changedOrNew[name] = hash(path.join(repo, name));
const evidenceHashes = {};
for (const name of fs.readdirSync(output).sort()) {
  const file = path.join(output, name);
  if (fs.statSync(file).isFile()) evidenceHashes[path.relative(project, file)] = hash(file);
}
console.log(JSON.stringify({schema: 1, date: '2026-10-20', phase: 114, version: '14.4.24',
  kind: 'isolated-native-acceptance-with-retained-test-harness-failure',
  productCodeChanged: false, unchanged, changedOrNew,
  progressSha256: hash(path.join(project, 'PROGRESS.md')),
  artifactHashes: {[developerBinary]: hash(path.join(project, developerBinary)),
    'tmp/native-ui-probe-20261020/native-ui-probe.app/Contents/MacOS/native-ui-probe':
      hash(path.join(project, 'tmp/native-ui-probe-20261020/native-ui-probe.app/Contents/MacOS/native-ui-probe'))},
  tests: {main, headers, spellingChecks: 34, nativePromptAndCancelChecks: prompt.checks,
    nativeSaveSelection: manual ? {checks: manual.checks, failures: manual.failures,
      normalExit: manual.normalExit, accepted: manualAccepted} : {accepted: false, pending: true}},
  retainedFailures: {initialHarness: {checks: 54, failures: 6},
    diagnosticHarness: {exitCode: 134, cause: 'NSSavePanel ok: not implemented; removed from harness'}},
  systemFullKeyboardAccessEnabled: prompt.fullKeyboardAccessEnabled,
  fullButtonTabTraversalAccepted: prompt.fullKeyboardAccessEnabled,
  voiceOver: {requiredForPersonalUse: false, tested: false, systemSettingsChanged: false},
  recoveryDefault: 'OFF', nativeScopeAccepted: manualAccepted,
  overallProductAcceptanceComplete: false, realNoteCopyAcceptancePending: true,
  automationStatusUnchanged: 'PAUSED', packageOrInstallPerformed: false, evidenceHashes}, null, 2));
