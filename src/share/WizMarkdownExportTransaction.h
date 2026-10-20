#pragma once

#include <QString>

#include <functional>

namespace WizMarkdownExportTransaction {

// Stages HTML/resources beside the destination, installs a complete resource
// directory, and commits Markdown last. Ordinary returned failures preserve the
// previous destination or retain the only recovery copy explicitly. This does
// not claim atomicity across power loss or concurrent writers.
bool write(const QString& destination, const QString& markdown,
           const std::function<bool(const QString&)>& exportHtml,
           QString& error);

// Ordinary HTML publication only, even when Markdown recovery is compiled ON.
// Empty rendered HTML preserves the saved archive's HTML bytes/BOM; otherwise
// the rendered body uses the existing UTF-8 BOM/resource rewrite contract.
// current must remain valid through staging and final publication. No saves,
// credentials, GUI events or persistent recovery are owned by this helper.
bool writeHtml(const QString& destination, const QString& rendered,
               const std::function<bool(const QString&)>& exportHtml,
               const std::function<bool()>& current, QString& error);

}
