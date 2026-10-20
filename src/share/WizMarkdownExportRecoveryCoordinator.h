#pragma once

#include "WizMarkdownExportRecovery.h"

namespace WizMarkdownExportRecoveryCoordinator {

// Dormant same-target decision facade; no App/UI integration. Inspect is read
// only, Cancel/Retain do no I/O. Only an explicitly supplied Confirm can remove
// a confirmed-dead lock or execute recovery. It never scans other destinations.
// Intent must be recorded and unlocked before any destination lock operation;
// the final result is appended only after our destination lock is released.
WizMarkdownExportRecovery::Result decide(const QString& destination,
    WizMarkdownExportRecovery::Decision decision);

}
