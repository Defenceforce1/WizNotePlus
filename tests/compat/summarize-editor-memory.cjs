// Read isolated probe logs only. Never label a small RSS delta as proof that
// native/graphics memory is stable, or conflate this fixture with the full app.
const fs = require('node:fs');
const path = require('node:path');
const files = process.argv.slice(2);
if (!files.length) throw Error('Usage: node summarize-editor-memory.cjs PROBE_LOG [...]');
function metric(samples, field) {
    const values = samples.map(x => x[field]);
    if (values.some(x => typeof x !== 'number' || !Number.isFinite(x) || x < 0)) return null;
    const warm = values.slice(Math.min(5, Math.floor(values.length / 2)));
    return {firstMiB: values[0] / 1024, lastMiB: values.at(-1) / 1024,
        deltaMiB: (values.at(-1) - values[0]) / 1024,
        afterWarmupMinMiB: Math.min(...warm) / 1024, afterWarmupMaxMiB: Math.max(...warm) / 1024};
}
const cases = files.map(file => {
    const lines = fs.readFileSync(file, 'utf8').split(/\r?\n/);
    // DOM success does not make a failed rendering backend a valid memory
    // comparison. Keep raw logs, but reject graphics-loss/error recovery runs.
    if (lines.some(x => /Unable to initialize SkSurface|SKIA: Failed to begin write access|Context lost during MakeCurrent|Failed to make current since context is marked as lost|GPU process exited unexpectedly/.test(x)))
        throw Error('Graphics failure invalidates memory comparison: ' + file);
    const reports = lines.filter(x => x.startsWith('MEMORY_PROBE '));
    if (reports.length !== 1 || lines.some(x => x.startsWith('FAIL ')))
        throw Error('Missing, failed or incomplete probe: ' + file);
    const probe = JSON.parse(reports[0].slice('MEMORY_PROBE '.length));
    const completion = probe.standalone
        ? /^Standalone Qt memory checks: \d+ passed, 0 failed$/
        : /^Native editor runtime checks: \d+ failures: 0$/;
    if (!lines.some(x => completion.test(x)))
        throw Error('Missing matching probe completion: ' + file);
    if (probe.standalone && !lines.includes('PASS temporary profile removed after QApplication shutdown'))
        throw Error('Standalone profile cleanup not confirmed: ' + file);
    const opened = probe.samples.filter(x => x.phase === 'four-open');
    const closed = probe.samples.filter(x => x.phase === 'all-closed');
    const expectedClosedProcesses = probe.expectedClosedProcessCount ?? 1;
    const images = probe.syntheticLargeImagesPerPage ?? 0;
    if (probe.hiddenCocoa && (probe.qtPlatform !== 'cocoa'
        || opened.some(x => !x.hiddenCocoa || x.visibleCocoa || x.visibleOffscreen
            || x.actualQuickBackends?.length !== 4 || x.actualQuickBackends.some(b => b.graphicsApi !== 6
                || b.windowClass !== 'QQuickWidgetOffscreenWindow'))))
        throw Error('Incomplete hidden Cocoa Metal observations: ' + file);
    if (images !== 0 && (images !== 2 || probe.largeImageWidth !== 2048 || probe.largeImageHeight !== 1536
        || probe.imagesDecodedBeforeSample !== true || opened.some(x => x.syntheticLargeImagesPerPage !== 2)))
        throw Error('Incomplete synthetic image/decode observations: ' + file);
    if (expectedClosedProcesses !== (probe.retainedNote ? 2 : 1))
        throw Error('Invalid retained-page process expectation: ' + file);
    if (!opened.length || opened.length !== closed.length
        || opened.some((x, i) => x.batch !== i || x.processCount < 1)
        || closed.some((x, i) => x.batch !== i || x.processCount !== expectedClosedProcesses || !x.available))
        throw Error('Incomplete batches or remaining child process: ' + file);
    const delayed = probe.samples.find(x => x.phase === 'all-closed-after-ten-seconds');
    const finallyClosed = probe.samples.find(x => x.phase === 'retained-tab-finally-closed');
    const afterPool = probe.samples.filter(x => x.phase === 'after-native-batch-pool');
    if (probe.nativeBatchPool && (afterPool.length !== closed.length
        || afterPool.some((x, i) => x.batch !== i || x.processCount !== expectedClosedProcesses)))
        throw Error('Incomplete native pool observations: ' + file);
    if (probe.retainedNote && (!delayed || delayed.processCount !== 2
        || !finallyClosed || finallyClosed.processCount !== 1))
        throw Error('Retained page did not remain alive or finish cleanup: ' + file);
    return {log: path.basename(file), batches: closed.length, pageOpenings: closed.length * 4,
        emptyControl: !!(probe.emptyControl ?? opened[0].emptyControl),
        visibleCocoa: !!(probe.visibleCocoa ?? opened[0].visibleCocoa),
        visibleOffscreen: !!opened[0].visibleOffscreen, sharedHost: probe.sharedHost ?? null,
        hiddenCocoa: !!probe.hiddenCocoa,
        imagesOnly: !!probe.imagesOnly,
        formulasPerPage: probe.formulasPerPage ?? null,
        actualQuickBackends: opened[0].actualQuickBackends ?? null,
        nestedLoopControl: probe.nestedLoopControl ?? null,
        retainedNote: probe.retainedNote ?? false, releaseGraphics: probe.releaseGraphics ?? false,
        nativeBatchPool: probe.nativeBatchPool ?? false,
        rhiControl: probe.rhiControl ?? '',
        stockView: probe.stockView ?? false,
        standalone: probe.standalone ?? false,
        lifecycleStress: probe.lifecycleStress ?? false,
        switchResizeStepsPerBatch: probe.switchResizeStepsPerBatch ?? 0,
        closeWhileHidden: probe.closeWhileHidden ?? false,
        syntheticLargeImagesPerPage: images,
        largeImageWidth: probe.largeImageWidth ?? 0,
        largeImageHeight: probe.largeImageHeight ?? 0,
        imagesDecodedBeforeSample: probe.imagesDecodedBeforeSample ?? false,
        qtVersion: probe.qtVersion ?? null,
        webEngine: probe.webEngine ?? null,
        chromium: probe.chromium ?? null,
        chromiumSecurityPatch: probe.chromiumSecurityPatch ?? null,
        quickBackend: probe.quickBackend ?? null,
        graphicsApi: probe.graphicsApi ?? null,
        sceneGraphBackend: probe.sceneGraphBackend ?? null,
        expectedClosedProcessCount: expectedClosedProcesses,
        namedPassChecks: lines.filter(x => x.startsWith('PASS ')).length,
        closedWidgetCounts: [...new Set(closed.map(x => x.widgetCount ?? null))],
        closedProfileObjectCounts: [...new Set(closed.map(x => x.profileDescendantObjects ?? null))],
        closedWindowCounts: [...new Set(closed.map(x => x.windowCount ?? null))],
        mainRss: metric(closed, 'mainRssKiB'), mainFootprint: metric(closed, 'mainFootprintKiB'),
        afterNativePoolFootprint: afterPool.length ? metric(afterPool, 'mainFootprintKiB') : null,
        delayedMainRssMiB: typeof delayed?.mainRssKiB === 'number' ? delayed.mainRssKiB / 1024 : null,
        delayedMainFootprintMiB: typeof delayed?.mainFootprintKiB === 'number' ? delayed.mainFootprintKiB / 1024 : null};
});
console.log(JSON.stringify({schema: 1,
    warning: 'RSS and native footprint differ. This synthetic workload does not prove absence of leaks or full-app performance.',
    cases}, null, 2));
