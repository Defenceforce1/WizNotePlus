#!/usr/bin/env node
'use strict';
// Restore only official same-version WebEngine headers in the isolated probe SDK.
// The archive contains libraries, but none are extracted or installed by this tool.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),cp=require('node:child_process');
const{pipeline}=require('node:stream/promises'),{Readable}=require('node:stream');
const output=process.argv[2];if(!output||!path.isAbsolute(output))throw new Error('Absolute existing probe directory required');
const sdk=path.join(output,'sdk');if(!fs.statSync(sdk).isDirectory())throw new Error('Prepare the Qt Base probe headers first');
const base='https://download.qt.io/online/qtsdkrepository/mac_x64/extensions/qtwebengine/6113/clang_64/extensions.qtwebengine.6113.clang_64/';
const name='6.11.3-0-202609240437qtwebengine-MacOS-MacOS_15-Clang-MacOS-MacOS_15-X86_64-ARM64.7z';
const archive=path.join(output,name);
const hash=algorithm=>crypto.createHash(algorithm).update(fs.readFileSync(archive)).digest('hex');
(async()=>{
  const checksum=await fetch(base+name+'.sha1',{signal:AbortSignal.timeout(20000)});
  if(!checksum.ok)throw new Error('Official checksum HTTP '+checksum.status);
  const expected=(await checksum.text()).trim();if(!/^[a-f0-9]{40}$/i.test(expected))throw new Error('Unexpected checksum format');
  const expectedBytes=152508438;
  const downloaded=fs.existsSync(archive)?fs.statSync(archive).size:0;
  // Installer metadata compressed size can differ slightly from HTTP payload;
  // the official payload checksum, not that estimate, decides completeness.
  const verifiedCache=downloaded>0&&hash('sha1')===expected.toLowerCase();
  if(!verifiedCache){
    if(downloaded>=expectedBytes)throw new Error('Full-size cached SDK failed checksum; refusing to append');
    console.log('Downloading the fixed 152 MB Qt WebEngine SDK archive; header extraction only');
    const response=await fetch(base+name,{signal:AbortSignal.timeout(600000),headers:downloaded?{Range:'bytes='+downloaded+'-'}:{}});
    if(!response.ok||!response.url.startsWith('https:'))throw new Error('SDK download must succeed over HTTPS');
    if(downloaded&&(response.status!==206||!response.headers.get('content-range')?.startsWith('bytes '+downloaded+'-')))
      throw new Error('Refusing to append without an exact partial-content response');
    console.log('Resuming from byte '+downloaded);
    await pipeline(Readable.fromWeb(response.body),fs.createWriteStream(archive,{flags:downloaded?'a':'wx'}));
  }
  if(hash('sha1')!==expected.toLowerCase())throw new Error('Official archive checksum mismatch; do not extract');
  const listing=cp.spawnSync('/usr/bin/tar',['-tf',archive],{encoding:'utf8',maxBuffer:32*1024*1024});
  if(listing.status!==0)throw new Error(listing.stderr);
  const all=listing.stdout.split('\n').filter(Boolean);
  if(all.some(p=>path.isAbsolute(p)||p.split('/').includes('..')))throw new Error('Unsafe archive path');
  const entries=all.filter(p=>/\/(QtWebEngineCore|QtWebEngineWidgets)\.framework\/(?:Headers(?:\/|$)|Versions\/A\/Headers(?:\/|$)|Versions\/Current$)/.test(p));
  if(entries.length<50)throw new Error('Too few WebEngine headers');
  const manifest=path.join(output,'webengine-header-entries.txt');fs.writeFileSync(manifest,entries.join('\n')+'\n');
  const extracted=cp.spawnSync('/usr/bin/tar',['-xf',archive,'-C',sdk,'-T',manifest],{encoding:'utf8'});
  if(extracted.status!==0)throw new Error(extracted.stderr);
  const header=fs.realpathSync(path.join(sdk,'lib/QtWebEngineCore.framework/Headers/QWebEnginePage'));
  if(!header.startsWith(sdk+path.sep))throw new Error('Header escaped isolated SDK');
  const result={qtVersion:'6.11.3',archiveUrl:base+name,officialSha1:expected,archiveSha256:hash('sha256'),
    archiveBytes:fs.statSync(archive).size,headerEntries:entries.length,noGlobalInstallation:true,noRuntimeExtraction:true};
  fs.writeFileSync(path.join(output,'webengine-header-sdk-provenance.json'),JSON.stringify(result,null,2)+'\n');
  console.log(JSON.stringify(result));
})().catch(error=>{console.error(error);process.exitCode=1;});
