#!/usr/bin/env node
// Read-only: never launch, install, register, sign, replace or delete an App.
import fs from 'node:fs';
import path from 'node:path';
import crypto from 'node:crypto';
import { spawnSync } from 'node:child_process';

const [workspaceArg, ...options] = process.argv.slice(2);
if (!workspaceArg || options.some(option => option !== '--include-installed')) {
  console.error('Usage: node verify-rollback-baseline.mjs WORKSPACE [--include-installed]');
  process.exit(2);
}
const workspace = fs.realpathSync(workspaceArg);
const manifestPath = path.join(workspace, 'outputs/baselines/main-14.4.16-20261017.json');
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
if (manifest.schema !== 1 || manifest.version !== '14.4.16') throw new Error('Unexpected baseline');
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const fileHash = filename => sha256(fs.readFileSync(filename));
let failures = 0;
function check(ok, label) {
  console.log(`${ok ? 'PASS' : 'FAIL'} ${label}`);
  if (!ok) failures++;
}
function inventory(root) {
  const entries = [];
  let files = 0, symlinks = 0, fileBytes = 0;
  function walk(relative) {
    const full = path.join(root, relative), stat = fs.lstatSync(full);
    if (stat.isSymbolicLink()) {
      symlinks++;
      entries.push([relative, 'symlink', fs.readlinkSync(full)]);
    } else if (stat.isDirectory()) {
      entries.push([relative, 'directory']);
      for (const name of fs.readdirSync(full).sort()) walk(relative ? `${relative}/${name}` : name);
    } else if (stat.isFile()) {
      files++;
      fileBytes += stat.size;
      entries.push([relative, 'file', stat.mode & 0o111, stat.size, fileHash(full)]);
    } else throw new Error(`Unsupported entry: ${relative}`);
  }
  walk('');
  entries.sort((a, b) => a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0);
  return {sha256: sha256(JSON.stringify(entries)), entries: entries.length, files, symlinks, fileBytes};
}
function verifyApp(app, label) {
  try {
    const actual = inventory(app);
    check(Object.keys(manifest.bundle).every(key => actual[key] === manifest.bundle[key]), `${label}: complete bundle inventory`);
    check(fileHash(path.join(app, 'Contents/MacOS/WizNotePlus')) === manifest.executableSha256, `${label}: executable`);
    const plist = path.join(app, 'Contents/Info.plist');
    for (const key of ['CFBundleShortVersionString', 'CFBundleVersion']) {
      const result = spawnSync('/usr/libexec/PlistBuddy', ['-c', `Print :${key}`, plist], {encoding: 'utf8'});
      check(result.status === 0 && result.stdout.trim() === manifest.version, `${label}: ${key}`);
    }
    const signed = spawnSync('/usr/bin/codesign', ['--verify', '--deep', '--strict', app], {encoding: 'utf8'});
    check(signed.status === 0, `${label}: strict nested signature`);
  } catch (error) {
    check(false, `${label}: ${error.message}`);
  }
}
for (const artifact of manifest.artifacts) {
  try { check(fileHash(path.join(workspace, artifact.path)) === artifact.sha256, artifact.path); }
  catch (error) { check(false, `${artifact.path}: ${error.message}`); }
}
verifyApp(path.join(workspace, manifest.preservedApp), 'preserved 14.4.16');
// Future installed candidates intentionally differ. Check them only on request.
if (options.includes('--include-installed')) verifyApp(manifest.installedApp, 'installed App against 14.4.16');
console.log(`Baseline verification failures: ${failures}`);
process.exitCode = failures ? 1 : 0;
