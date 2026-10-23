# Local welcome initialization benchmark

Phase230 adds only a diagnostic mode, not a product hook. The caller must create
the exact synthetic marker and provider fixture. Real local Welcome HTML, styles,
libraries and business code are loaded; only QWebChannel is replaced by the
synthetic transport. External URL requests are blocked. The Cocoa view is not
exposed and each profile lives in QTemporaryDir.

`reader-opening FILES welcome-benchmark OUTPUT` runs one warmup and five measured
loads in one isolated profile. A document-creation MutationObserver records
performance.now when the body is visible, the synthetic unread list exists and
datepicker is initialized; a separate timestamp follows two animation frames.
Each load checks both milestones and their monotonic ordering. A group has20
checks. It does not measure application startup, real storage, RSS or a foreground
window's screen presentation.

An optional business-input.js replaces only the owned Welcome business script in
the temporary HTML. The phase230 attribution fixture uses226 byte-identical
libraries and removes only the exact 1000ms listEvents wait. It never edits226,
the live product, or an account. This distinguishes a fixed business delay from
library-performance claims. Three groups pass60 checks; timings and limitations
are in docs/component-benefits-2026-10-23.md.

The packaged associated checks are separate: Welcome27/27, Todo33, actual query
replay/storage helper26 =113. Four screenshots equal the accepted229 source
images. Existing formula/PDF resources and frameworks are unchanged by complete
inventory, so those unrelated suites are not rerun. No migration shim is shipped.
