#pragma once
#include <QByteArray>
#include <QStringList>
#include <QVector>

namespace ContentCase {
struct Scalar {
    QString text;
    qint64 begin = -1, end = -1;
};
struct Field {
    int index = -1;
    QString name, location;
    QStringList parents;
    QVector<Scalar> values;
    QVector<int> binaryLengthOffsets;
};
// A reference inventory, not an editable document or an editor/rendering object.
struct Document {
    QString encoding, offsets;
    bool compressed = false, binary = false, valid = false;
    // Completion of the supported reference regions is independent of syntax
    // validity and of whether a reference edit may be planned.
    bool decoded = false, referenceScanComplete = false, syntaxWarning = false;
    bool shapeImagesComplete = false;
    QVector<Field> fields;
    QStringList diagnostics;
    QStringList discoveryFailures;
};
Document inspectDocument(const QByteArray &bytes, const QString &family);
bool looksLikeResource(const QString &value);
struct ReferenceEdit { int fieldIndex, scalarIndex; QString expected, replacement; };
bool patchDocument(const QByteArray &input, const QString &family, const QVector<ReferenceEdit> &edits,
                   QByteArray &output, QString &error);
}
