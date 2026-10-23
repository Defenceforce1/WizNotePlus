// Read-only startup checks. Never opens a GUI/account or changes login settings.
const fs = require('fs'), path = require('path'), cp = require('child_process'), crypto = require('crypto');
const assert = require('assert/strict');
const [folderArg, wrapperArg] = process.argv.slice(2);
assert(folderArg && wrapperArg, 'usage: node test-candidate-launcher.cjs PACKAGE_DIR CURRENT_COMMAND');
const folder = path.resolve(folderArg), wrapper = path.resolve(wrapperArg), launcher = folder + '/启动测试版.command';
const stable = path.dirname(folder) + '/WizNotePlus-测试资料', binary = folder + '/WizNotePlus Apple Silicon.app/Contents/MacOS/WizNotePlus';
const template = fs.readFileSync(path.resolve(__dirname, '../../build/osx/launch-test-candidate.sh.in'), 'utf8');
const sha = b => crypto.createHash('sha256').update(b).digest('hex'); let checks = 0;
function check(ok, text) { assert(ok, text); checks++; }
const before = { binary: sha(fs.readFileSync(binary)), wrapper: sha(fs.readFileSync(wrapper)) };
function metadata(p) {
  const rows = []; function walk(q, rel) {
    const s = fs.lstatSync(q); assert(!s.isSymbolicLink(), 'plain fixed store');
    rows.push([rel, s.dev, s.ino, s.mode, s.isFile() ? s.size : null, s.isFile() ? s.mtimeMs : null]);
    if (s.isDirectory()) for (const n of fs.readdirSync(q).sort()) walk(q + '/' + n, rel + '/' + n);
  }
  walk(p, ''); return sha(JSON.stringify(rows));
}
const original = metadata(stable), version = cp.execFileSync('/usr/libexec/PlistBuddy', ['-c', 'Print :WizNotePlusTrialBuild', folder + '/WizNotePlus Apple Silicon.app/Contents/Info.plist'], { encoding: 'utf8' }).trim();
check(template.replaceAll('@BUILD@', version).replaceAll('@EXE_SHA256@', before.binary) === fs.readFileSync(launcher, 'utf8'), 'launcher exactly matches reusable template');
for (const file of [launcher, wrapper]) check(cp.spawnSync('/bin/bash', ['-n', file]).status === 0, 'shell syntax');
const env = { ...process.env, WIZNOTE_DATA_STORE: '/nonexistent/inherited-store-must-not-be-used' };
for (const [label, file] of [['candidate', launcher], ['fixed-entry', wrapper], ['repeated-fixed-entry', wrapper]]) {
  const r = cp.spawnSync('/bin/bash', [file, '--check'], { env, encoding: 'utf8', timeout: 30000 });
  check(r.status === 0, label + ' non-GUI startup');
  check((r.stdout + r.stderr).includes('独立资料库路径： ' + stable), label + ' uses same fixed store despite inherited override');
  check(metadata(stable) === original, label + ' leaves all data-file metadata unchanged');
}
const invalid = cp.spawnSync('/bin/bash', [wrapper, '--invalid'], { env, encoding: 'utf8', timeout: 30000 });
check(invalid.status === 2 && (invalid.stdout + invalid.stderr).includes('用法'), 'invalid argument rejected');
check(!fs.existsSync(folder + '/测试资料'), 'no version-specific store recreated');
check(sha(fs.readFileSync(binary)) === before.binary && sha(fs.readFileSync(wrapper)) === before.wrapper, 'App and launcher unchanged during tests');
check(metadata(stable) === original, 'retained user store unchanged after all checks');
console.log(JSON.stringify({ accepted: true, checks, guiOpened: false, accountOpened: false, networkUsed: false,
  fixedStoreReused: true, noteContentsRead: false, loginSettingsChanged: false, actualAccountLoginAccepted: false }));
