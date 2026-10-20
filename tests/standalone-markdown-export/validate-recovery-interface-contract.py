#!/usr/bin/env python3
import json
import pathlib
import sys


checks = 0


def require(condition, message):
    global checks
    checks += 1
    if not condition:
        raise ValueError(message)


path = pathlib.Path(sys.argv[1] if len(sys.argv) > 1
                    else pathlib.Path(__file__).with_name("recovery-interface-contract.json"))
contract = json.loads(path.read_text(encoding="utf-8"))

require(set(contract) == {"schema", "status", "protocol", "operations", "audit",
                           "ui", "exitCoordination", "productIntegration"},
        "unexpected top-level contract fields")
require(contract["schema"] == 1, "unsupported contract schema")
require(contract["status"] == "experimental-default-disabled",
        "contract must not imply product integration")

protocol = contract["protocol"]
require(protocol["version"] == 1, "unsupported protocol version")
recovery = protocol["recoveryDirectory"]
require(recovery["sameParentAsDestination"], "recovery directory must share destination parent")
require(recovery["permissions"] == "0700", "recovery directory must be private")
require(recovery["manifestAtomicCommit"] and recovery["fileAndParentDirectorySync"],
        "manifest durability requirements missing")
require(not recovery["automaticDeletionOfUnknownState"],
        "unknown recovery state must never be automatically deleted")

manifest = protocol["manifest"]
require(set(manifest["exactFields"]) == {
            "schema", "destinationName", "resourcesName", "oldMarkdownExisted",
            "oldMarkdownSha256", "newMarkdownSha256", "oldResourcesExisted",
            "oldResourcesSha256", "newResourcesExisted", "newResourcesSha256"},
        "manifest field whitelist changed")
require(manifest["destinationAndResourceNamesMustBeBasenames"],
        "manifest paths must remain same-parent basenames")
require(manifest["unknownSchemaOrFieldsRefused"] and manifest["liveStateRehashedBeforeMutation"],
        "strict manifest/live revalidation required")
require(not manifest["persistentDigestCacheCanAuthorizeRecovery"],
        "cache must not authorize recovery")

triggers = protocol["triggers"]
require(not triggers["broadStartupScan"] and triggers["sameDestinationExport"],
        "recovery must be targeted rather than a broad startup scan")
require(not triggers["automaticRecovery"], "recovery must not be automatic")

locks = protocol["locks"]
require(locks["destinationPattern"] != locks["auditPattern"],
        "destination and audit locks must have separate namespaces")
require(not locks["nestedLocksAllowed"], "destination and audit locks must never be nested")
require(not locks["activeOwnerTakeover"] and not locks["silentDeadOwnerTakeover"],
        "lock takeover must be explicit and never target an active owner")
require(locks["confirmedDeadOwnerRequirements"]
        == ["same-host", "same-application", "pid-not-running"],
        "dead-owner confirmation requirements changed")
require(locks["malformedOrForeignLockRefused"] and locks["symlinkRefused"],
        "unsafe destination lock forms must be refused")

require(set(protocol["knownStates"]) == {
            "prepared", "old-resources-reserved", "new-resources-installed",
            "markdown-committed", "rollback-new-resources-returned",
            "rollback-old-resources-restored"}
        and protocol["unknownStateAction"] == "refuse-and-retain",
        "recognized recovery evidence states changed")

operations = contract["operations"]
for action in ("inspect", "cancel", "retain"):
    require(not operations[action]["mutatesRecoverySite"],
            f"{action} must not mutate the recovery site")
sequence = operations["confirm"]["sequence"]
require(sequence[0] == "append-and-release-audit-intent"
        and sequence[-1] == "append-and-release-audit-result",
        "confirm audit ordering changed")
require(sequence[1:5] == ["acquire-destination-lock", "revalidate-live-state",
                         "recover-known-state-or-refuse", "release-destination-lock"],
        "destination lock must cover revalidation and recovery only")
require(operations["confirm"]["auditIntentFailureBlocksRecovery"],
        "audit intent must precede confirmed recovery")
require(not operations["confirm"]["auditResultFailureRevertsRecovery"],
        "post-recovery audit failure must not invent a rollback")
require(operations["result"]["recoveryAndAuditOutcomesReportedSeparately"],
        "recovery and audit outcomes must remain separate")

audit = contract["audit"]
require(audit["rootProvider"] == "QStandardPaths::AppLocalDataLocation"
        and audit["relativeDirectory"] == "recovery-audit/v1",
        "audit root ownership changed")
require(audit["directoryPermissions"] == "0700" and audit["filePermissions"] == "0600",
        "audit permissions must remain private")
require(set(audit["exactEventFields"])
        == {"destinationIdentity", "decision", "event"},
        "audit event field whitelist changed")
require(not audit["containsFullPathOrNoteContent"], "audit must minimize user data")
require(audit["allowedDecisionEvents"] == {
            "confirm": ["requested", "none", "rolled-back", "finalized", "busy", "refused"],
            "retain": ["retained"]},
        "audit decision/event pairs must remain finite and disjoint")
require(audit["maximumArchives"] == 2 and audit["maximumEntriesPerSegment"] == 256
        and audit["maximumBytesPerSegment"] == 65536,
        "bounded audit defaults changed")
require(not audit["timeBasedRetention"],
        "timestamp-free audit cannot claim time-based retention")
require(audit["clearNeverDeletesRecoverySites"]
        and audit["uninstallNeverScansExportDestinations"],
        "audit cleanup must not delete recovery evidence")
require(audit["userCanClear"] and audit["uninstallMayRemoveOnlyAppOwnedAuditRoot"],
        "audit lifecycle ownership changed")

ui = contract["ui"]
require(ui["defaultAction"] == "retain" and ui["cancelEquivalent"] == "retain",
        "safe UI default must retain evidence")
require(not ui["automaticCountdown"], "recovery must not use an automatic countdown")
require(set(ui["actions"]) == {"inspect", "retain", "confirm"},
        "unexpected recovery actions")
require(ui["showsFullPathOnlyInUserOpenedDetails"],
        "full paths must not appear before the user opens details")
require(all(ui["accessibility"].values()), "all accessibility requirements are mandatory")

exit_policy = contract["exitCoordination"]
require(exit_policy["openPromptBecomes"] == "retain"
        and not exit_policy["modifiesRecoverySiteDuringExit"],
        "exit must retain recovery evidence")
require(not exit_policy["blocksLocalNoteSave"]
        and not exit_policy["startsAdditionalCloudSync"]
        and not exit_policy["changesThreeSecondQuitSyncPolicy"],
        "recovery UI must not alter established quit behavior")
require(exit_policy["directAboutToQuitRetainsBeforeCleanup"]
        and not exit_policy["directAboutToQuitStartsExtraSaveOrSync"],
        "direct exit must retain before cleanup without adding save or sync attempts")
require(exit_policy["auditRetainEventBestEffort"]
        and not exit_policy["startsRetainAuditDuringQuit"]
        and exit_policy["cancelsQueuedRetainAuditOnQuit"],
        "quit must skip new retain I/O and cancel optional queued logging")
require(not exit_policy["retainAuditFailureBlocksExit"]
        and not exit_policy["settledWaitsForRetainAudit"],
        "optional audit must never gate retention or application cleanup")
require(not exit_policy["runningRetainAuditHasHardDeadline"],
        "do not claim a hard deadline for already-running filesystem I/O")

integration = contract["productIntegration"]
require(not integration["enabledByDefault"], "experimental recovery must remain disabled")
require(integration["experimentalBuildOption"] == "WIZNOTE_ENABLE_MARKDOWN_RECOVERY"
        and integration["ordinaryWriterRequiresFreshDestinationLock"],
        "experimental App continuation requires fresh destination reservation")
require(integration["ordinaryExportCreatesPersistentRecoverySite"]
        and not integration["successfulRecoveryAutomaticallyRetriesExport"],
        "gated ordinary writer must prepare durable evidence without automatic recovery retry")
require(integration["publicationRequiresCurrentProcessOneAttemptProof"]
        and integration["markdownCommittedAfterResourceInstallation"],
        "publication must require this preparation and commit Markdown last")
require(not integration["ordinaryFailureMayAdoptOtherRecoverySite"]
        and not integration["postCommitFailureMayUndoCommittedBody"],
        "failure handling must not adopt other sites or undo a committed body")
require("persistent-digest-cache" in integration["diagnosticOnlyComponents"],
        "persistent cache must remain diagnostic")
require(set(integration["requiredBeforeEnablement"]) == {
            "product-core-unit-tests", "ordinary-export-regression",
            "forced-termination-regression", "ui-accessibility-tests",
            "quit-coordination-tests", "full-product-regression"},
        "pre-enablement test gates changed")

print(json.dumps({"schema": contract["schema"], "checks": checks, "failures": 0,
                  "status": contract["status"]}, separators=(",", ":")))
