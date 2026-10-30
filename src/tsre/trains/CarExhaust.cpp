#include <tsre/trains/CarExhaust.h>

QString exhaustKind(QStringView name) {
    QString kind = name.toString().toLower();
    if (kind.endsWith(QLatin1String("fx")))
        kind.chop(2);
    if (kind.startsWith(QLatin1String("exhaust"))) {
        bool numbered = false;
        kind.mid(7).toInt(&numbered);
        if (numbered)
            kind = QStringLiteral("exhaust");
    }
    return kind;
}

QString exhaustValueName(const QString &kind, QAnyStringView field) {
    return QLatin1String("fx.") + kind + QLatin1Char('.') + field.toString();
}

CarExhaust testExhaust(const QStringList &kinds) {
    CarExhaust exhaust;
    for (const QString &kind : kinds) {
        // A diesel's exhaust, a steam loco's stack (smoke and steam in
        // puffs), anything else white steam.
        ExhaustOutput out;
        if (kind == QLatin1String("exhaust") || kind == QLatin1String("generator")) {
            out = {0.5f, 10.0f, 300.0f, 0.1f, 0.4f, 0.0f, 0};
        } else if (kind == QLatin1String("stack")) {
            out = {2.0f, 30.0f, 150.0f, 0.8f, 0.3f, 4.0f, 0};
        } else if (kind.startsWith(QLatin1String("safetyvalves"))) {
            out = {1.0f, 100.0f, 200.0f, 1.0f, 0.0f, 0.0f, 0};
        } else if (kind.startsWith(QLatin1String("heating"))) {
            out = {0.02f, 5.0f, 110.0f, 1.0f, 0.0f, 0.0f, 0};
        } else {
            out = {0.3f, 40.0f, 120.0f, 1.0f, 0.0f, 0.0f, 0};
        }
        exhaust.outputs.push_back(out);
    }
    return exhaust;
}
