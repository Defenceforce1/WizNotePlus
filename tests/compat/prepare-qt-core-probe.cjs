#!/usr/bin/env node
'use strict';
// Temporary header-only SDK for a standalone Core probe. No global install,
// framework modifications, executable extraction, or main-app rebuild.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process');
const {pipeline}=require('node:stream/promises');const{Readable}=require('node:stream');
const output=process.argv[2];if(!output||!path.isAbsolute(output))throw new Error('Absolute temporary probe directory required');
const base='https://download.qt.io/online/qtsdkrepository/mac_x64/desktop/qt6_6113/qt6_6113/qt.qt6.6113.clang_64/';
const archiveName='6.11.3-0-202609240332qtbase-MacOS-MacOS_15-Clang-MacOS-MacOS_15-X86_64-ARM64.7z';
const archive=path.join(output,archiveName),sdk=path.join(output,'sdk');
const frameworks=(process.env.WIZNOTE_PROBE_FRAMEWORKS||'QtCore').split(',');
if(!frameworks.includes('QtCore')||frameworks.some(name=>!['QtCore','QtGui','QtPrintSupport','QtWidgets','QtNetwork','QtOpenGL'].includes(name)))
  throw new Error('Only the fixed Qt Base header probes are supported');
const digest=(file,algorithm)=>crypto.createHash(algorithm).update(fs.readFileSync(file)).digest('hex');
(async()=>{
  fs.mkdirSync(output,{recursive:true});fs.mkdirSync(sdk,{recursive:true});
  const checkResponse=await fetch(base+archiveName+'.sha1',{signal:AbortSignal.timeout(20000)});
  if(!checkResponse.ok)throw new Error('Checksum HTTP '+checkResponse.status);
  const expected=(await checkResponse.text()).trim();if(!/^[0-9a-f]{40}$/i.test(expected))throw new Error('Unexpected official checksum format');
  if(!fs.existsSync(archive)){
    console.log('Downloading fixed Qt 6.11.3 qtbase archive; only Core headers will be extracted');
    const response=await fetch(base+archiveName,{signal:AbortSignal.timeout(180000)});
    if(!response.ok||!response.url.startsWith('https:'))throw new Error('Archive download must succeed over HTTPS');
    await pipeline(Readable.fromWeb(response.body),fs.createWriteStream(archive,{flags:'wx'}));
  }
  if(digest(archive,'sha1').toLowerCase()!==expected.toLowerCase())throw new Error('Official archive SHA-1 mismatch; do not extract');
  const list=cp.spawnSync('/usr/bin/tar',['-tf',archive],{encoding:'utf8',maxBuffer:32*1024*1024});
  if(list.status!==0)throw new Error(list.stderr);
  const all=list.stdout.split('\n').filter(Boolean);
  if(all.some(p=>path.isAbsolute(p)||p.split('/').includes('..')))throw new Error('Unsafe archive entry path');
  const entries=all.filter(p=>frameworks.some(name=>new RegExp('/'+name+'\\.framework/(?:Headers(?:/|$)|Versions/A/Headers(?:/|$)|Versions/Current$)').test(p)));
  if(entries.length<100)throw new Error('Archive Core header selection is unexpectedly small');
  const manifest=path.join(output,frameworks.length===1?'core-header-entries.txt':'layout-header-entries.txt');fs.writeFileSync(manifest,entries.join('\n')+'\n');
  const unpack=cp.spawnSync('/usr/bin/tar',['-xf',archive,'-C',sdk,'-T',manifest],{encoding:'utf8',maxBuffer:1024*1024});
  if(unpack.status!==0)throw new Error(unpack.stderr);
  const core=entries.find(p=>p.includes('/QtCore.framework/'));
  const frameworkDirectory=path.join(sdk,core.slice(0,core.indexOf('/QtCore.framework/')));
  const header=fs.realpathSync(path.join(frameworkDirectory,'QtCore.framework/Headers/QCoreApplication'));
  if(!header.startsWith(sdk+path.sep))throw new Error('Extracted header escapes isolated SDK');
  const source=fs.readFileSync(path.resolve(__dirname,'../../src/gui/documentviewer/DocumentLoaderSaver.h'),'utf8');
  const match=source.match(/class WizWaitEvent\s*\{[\s\S]*?\n\};/);
  if(!match)throw new Error('Source wait primitive extraction anchor changed');
  const candidate=!!process.env.WIZNOTE_STICKY_WAIT_CANDIDATE;
  const extracted=path.join(output,candidate?'WizWaitEvent-candidate-extracted.h':'WizWaitEvent-extracted.h');
  fs.writeFileSync(extracted,'// Extracted unchanged from DocumentLoaderSaver.h; upstream/project GPL-3.0 applies.\n'+match[0]+'\n');
  const result={qtVersion:'6.11.3',archiveUrl:base+archiveName,archiveBytes:fs.statSync(archive).size,
    officialSha1:expected,archiveSha256:digest(archive,'sha256'),headerEntries:entries.length,frameworkDirectory,frameworks,
    waitClassSha256:crypto.createHash('sha256').update(match[0]).digest('hex'),noGlobalInstallation:true};
  fs.writeFileSync(path.join(output,candidate?'candidate-wait-header-provenance.json':frameworks.length===1?'header-sdk-provenance.json':'layout-header-sdk-provenance.json'),JSON.stringify(result,null,2)+'\n');console.log(JSON.stringify(result,null,2));
})().catch(error=>{console.error(error);process.exitCode=1;});
