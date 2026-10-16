// Native layout-only diagnostic using the installed Qt Gui runtime.
// No GUI, user settings, note, file export or browser engine is created.
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QFile>
#include <QtGui/QPageLayout>
#include <QtGui/QPageSize>
#include <cmath>
#include <cstdio>

int main(int argc, char** argv)
{
    const struct Sample { const char* name; QPageLayout::Unit unit; double value; } samples[] = {
        {"Millimeter", QPageLayout::Millimeter, 5},
        {"Point", QPageLayout::Point, 72},
        {"Inch", QPageLayout::Inch, 1},
        {"Pica", QPageLayout::Pica, 6},
        {"Didot", QPageLayout::Didot, 72},
        {"Cicero", QPageLayout::Cicero, 6}
    };
    QJsonArray records;
    int failures = 0;
    for (const auto& sample : samples) {
        const QMarginsF requested(sample.value, sample.value, sample.value, sample.value);
        // Same constructor as saveAsPDF: the omitted unit defaults to Point.
        const QPageLayout baseline(QPageSize(QPageSize::A4), QPageLayout::Portrait, requested);
        // Diagnostic explicit-unit control only; not a production export change.
        const QPageLayout control(QPageSize(QPageSize::A4), QPageLayout::Portrait, requested, sample.unit);
        const double before = baseline.margins(QPageLayout::Millimeter).top();
        const double after = control.margins(QPageLayout::Millimeter).top();
        const bool baselineUsesPoints = baseline.units() == QPageLayout::Point
            && std::abs(before - sample.value * 25.4 / 72) < .011;
        const bool millimeterValue = sample.unit != QPageLayout::Millimeter || after == 5;
        const bool inchValue = sample.unit != QPageLayout::Inch || std::abs(after - 25.4) < .001;
        const bool pointValue = sample.unit != QPageLayout::Point || std::abs(after - 25.4) < .001;
        const bool expected = baseline.isValid() && control.isValid() && baselineUsesPoints && millimeterValue && inchValue && pointValue;
        failures += !expected;
        records.append(QJsonObject{{"unit", sample.name}, {"settingIndex", int(sample.unit)}, {"entered", sample.value},
            {"baselineMarginMm", before}, {"explicitUnitControlMarginMm", after}, {"expected", expected}});
    }
    const QJsonDocument result(QJsonObject{{"kind", "native Qt page-layout units only; NOT a PDF export or destination test"},
        {"qtRuntime", qVersion()}, {"records", records}, {"failures", failures}});
    const QByteArray json = result.toJson();
    if (argc == 2) {
        QFile output(QString::fromLocal8Bit(argv[1]));
        if (!output.open(QIODevice::WriteOnly | QIODevice::NewOnly) || output.write(json) != json.size()) return 2;
    }
    std::fwrite(json.constData(), 1, size_t(json.size()), stdout);
    return failures ? 1 : 0;
}
