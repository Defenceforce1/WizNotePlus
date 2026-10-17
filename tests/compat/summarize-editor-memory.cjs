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
    const reports = lines.filter(x => x.startsWith('MEMORY_PROBE '));
    if (reports.length !== 1 || lines.some(x => x.startsWith('FAIL '))
        || !lines.some(x => /^Native editor runtime checks: \d+ failures: 0$/.test(x)))
        throw Error('Missing, failed or incomplete probe: ' + file);
    const probe = JSON.parse(reports[0].slice('MEMORY_PROBE '.length));
    const opened = probe.samples.filter(x => x.phase === 'four-open');
    const closed = probe.samples.filter(x => x.phase === 'all-closed');
    if (!opened.length || opened.length !== closed.length
        || opened.some((x, i) => x.batch !== i || x.processCount < 1)
        || closed.some((x, i) => x.batch !== i || x.processCount !== 1 || !x.available))
        throw Error('Incomplete batches or remaining child process: ' + file);
    const delayed = probe.samples.find(x => x.phase === 'all-closed-after-ten-seconds');
    return {log: path.basename(file), batches: closed.length, pageOpenings: closed.length * 4,
        emptyControl: !!opened[0].emptyControl, visibleCocoa: !!opened[0].visibleCocoa,
        visibleOffscreen: !!opened[0].visibleOffscreen, sharedHost: probe.sharedHost ?? null,
        nestedLoopControl: probe.nestedLoopControl ?? null,
        namedPassChecks: lines.filter(x => x.startsWith('PASS ')).length,
        closedWidgetCounts: [...new Set(closed.map(x => x.widgetCount ?? null))],
        closedProfileObjectCounts: [...new Set(closed.map(x => x.profileDescendantObjects ?? null))],
        mainRss: metric(closed, 'mainRssKiB'), mainFootprint: metric(closed, 'mainFootprintKiB'),
        delayedMainRssMiB: typeof delayed?.mainRssKiB === 'number' ? delayed.mainRssKiB / 1024 : null,
        delayedMainFootprintMiB: typeof delayed?.mainFootprintKiB === 'number' ? delayed.mainFootprintKiB / 1024 : null};
});
console.log(JSON.stringify({schema: 1,
    warning: 'RSS and native footprint differ. This synthetic workload does not prove absence of leaks or full-app performance.',
    cases}, null, 2));
