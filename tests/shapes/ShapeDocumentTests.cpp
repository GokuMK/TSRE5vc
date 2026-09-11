#include <QCoreApplication>
#include <QDebug>
#include <QDirIterator>
#include <QFile>
#include <QtEndian>
#include <iostream>
#include <limits>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/shape/SFileDocument.h>
// Supply the same bundled codec used by TSRE, without linking the GL
// application.
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <mzip/miniz/miniz.h>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    using SFileDetail::Document;
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const QString &message) {
        ++checks;
        if (!ok) {
            ++failures;
            std::cerr << message.toStdString() << std::endl;
        }
    };
    if (argc > 1) {
        QDirIterator files(QString::fromLocal8Bit(argv[1]), QDir::Files,
                           QDirIterator::Subdirectories);
        while (files.hasNext()) {
            const auto path = files.next();
            if (!path.endsWith(".s", Qt::CaseInsensitive))
                continue;
            for (int iteration = 0; iteration < 3; ++iteration) {
                Document doc;
                QString error;
                check(doc.read(path) && !doc.damaged, "corpus parse " + path);
                const auto bytes = doc.encode(doc.binary, false, error);
                check(error.isEmpty(), "corpus encode " + path + ": " + error);
                if (iteration == 0 && doc.binary) {
                    QFile originalFile(path);
                    check(originalFile.open(QIODevice::ReadOnly),
                          "open original payload for independent comparison");
                    auto originalBytes = originalFile.readAll();
                    QByteArray payload = originalBytes.mid(16);
                    if (doc.compressed) {
                        QByteArray framed(4, Qt::Uninitialized);
                        qToBigEndian(qFromLittleEndian<quint32>(originalBytes.constData() + 8),
                                     framed.data());
                        payload = qUncompress(framed + payload);
                    }
                    check(bytes.mid(32) == payload.mid(16),
                          "independent original binary payload preserved exactly: " + path);
                }
                if (iteration == 0) {
                    Document requestedCompact;
                    check(requestedCompact.read(path, false, true) && !requestedCompact.damaged &&
                              requestedCompact.compact &&
                              requestedCompact.blockCount() <= doc.blockCount() &&
                              requestedCompact.scalarCount() <= doc.scalarCount(),
                          "corpus pre-load Compact path stays bounded and omits source "
                          "data: " +
                              path);
                }
                Document copy;
                check(copy.readBytes(bytes) && !copy.damaged, "corpus reload " + path);
                check(copy.encode(doc.binary, false, error) == bytes && error.isEmpty(),
                      "corpus semantic roundtrip " + path + ": " + error);
            }
        }
    }
    Document source;
    QString error;
    check(source.read(QString(SHAPE_FIXTURE_DIR) + "/coverage.s") && !source.damaged,
          "coverage source");
    const auto canonical = source.encode(true, false, error);
    check(!canonical.isEmpty() && error.isEmpty(),
          "all grammar families have binary layouts: " + error);
    for (bool binary : {false, true})
        for (bool compressed : {false, true}) {
            auto bytes = source.encode(binary, compressed, error);
            Document copy;
            check(error.isEmpty() && copy.readBytes(bytes) && !copy.damaged,
                  "encoding matrix load: " + error + copy.diagnostics.join("; "));
            check(copy.encode(true, false, error) == canonical && error.isEmpty(),
                  "semantic fields, labels, optional presence and sequences survive "
                  "round trip: " +
                      error);
        }
    QString exportedText, textError;
    check(SimisTextReader::decode(source.encode(false, false, error), exportedText, textError) &&
              exportedText.startsWith("SIMISA@@@@@@@@@@JINX0s1t______\r\n"),
          "text export has the exact 16-character SIMIS subheader required by "
          "external readers");
    Document direct;
    check(direct.readBytes("shape ( animations ( 1 animation ( 1 1 anim_nodes ( 1 anim_node ( "
                           "controllers ( 1 tcb_rot ( 1 slerp_rot ( 0 0 0 0 1 ) ) ) ) ) ) ) )"),
          "direct quaternion key variant");
    Document directCopy;
    check(directCopy.readBytes(direct.encode(true, true, error)) && !directCopy.damaged &&
              error.isEmpty(),
          "direct quaternion key binary layout");
    Document malformed;
    for (bool compact : {false, true}) {
        const QByteArray animationTail = "shape ( points ( 0 ) animations ( 1 animation ( 1 1 "
            "anim_nodes ( 2 anim_node A ( controllers ( 0 ) "
            "anim_node B ( controllers ( 0 ) ) ) ) ) )";
        check(malformed.readBytes(animationTail, false, compact) && malformed.damaged &&
                  malformed.damageConfinedToAnimations(), "animation tail damage classification");
        Document copied = malformed;
        check(copied.damageConfinedToAnimations() && copied.original == animationTail &&
                  copied.encode(false, false, error).isEmpty(),
              "copy preserves animation damage and refuses silent repair");
        check(malformed.readBytes("shape ( animations ( ( ) ) ) junk", false, compact) &&
                  !malformed.damageConfinedToAnimations(),
              "trailing damage cannot be hidden by animation damage");
        check(malformed.readBytes("shape ( points ( ( ) ) animations ( ( ) ) )", false, compact) &&
                  !malformed.damageConfinedToAnimations(),
              "static syntax damage cannot be hidden by animation damage");
        check(malformed.readBytes("shape ( points ( 0 ) ) shape ( animations ( ( ) ) )",
                                  false, compact) && !malformed.damageConfinedToAnimations(),
              "extra roots do not inherit main-shape animation recovery");
        check(malformed.readBytes("shape ( points ( 0 ) animations ( 0 ) )", false, compact) &&
                  !malformed.damaged && !malformed.damageConfinedToAnimations(),
              "reload resets animation damage classification");
    }
    // Extra binary tail survives known-field editing; conversion is refused.
    Document tail;
    auto extra = canonical;
    // Independent tiny binary leaf: root shape contains shape_header with 3
    // dwords.
    auto word = [](QByteArray &b, quint32 n) {
        auto at = b.size();
        b.resize(at + 4);
        qToLittleEndian(n, b.data() + at);
    };
    QByteArray leaf;
    word(leaf, TS::shape_header);
    word(leaf, 13);
    leaf.append('\0');
    word(leaf, 1);
    word(leaf, 2);
    word(leaf, 3);
    QByteArray payload;
    word(payload, TS::shape);
    word(payload, 1 + leaf.size());
    payload.append('\0');
    payload += leaf;
    const QByteArray header = "SIMISA@@@@@@@@@@JINX0s1b________";
    check(tail.readBytes(header + payload) && !tail.damaged, "extra leaf payload tolerated");
    tail.root.child("shape_header").setScalar(0, "ff");
    Document changed;
    check(changed.readBytes(tail.encode(true, true, error)) &&
              changed.root.child("shape_header")->tail() == tail.root.child("shape_header")->tail(),
          "extra tail retained after edit");
    tail.encode(false, false, error);
    check(!error.isEmpty(), "opaque tail cross-encoding refused");
    // Truncations and bounded length corruptions must terminate and remain
    // save-ineligible.
    for (int size = 0; size < canonical.size(); size += 17) {
        Document truncated;
        truncated.readBytes(canonical.left(size));
        check(truncated.damaged, "truncation diagnosed");
    }
    for (int at = 32; at < canonical.size(); at += 31) {
        auto malformed = canonical;
        malformed[at] = char(0xff);
        Document copy;
        copy.readBytes(malformed);
        if (copy.damaged) {
            copy.encode(true, false, error);
            check(!error.isEmpty(), "damaged save refused");
        }
    }
    QByteArray badZip = "SIMISA@F";
    word(badZip, 8);
    badZip += "@@@@";
    badZip += qCompress(QByteArray(10000, 'x')).mid(4);
    Document bomb;
    check(!bomb.readBytes(badZip) && bomb.damaged, "inflation cannot exceed declared size");
    Document missing;
    check(missing.readBytes("shape ( points ( 999 point ( 1 2 3 ) ) future ( \"a)b\" ) )"),
          "counts do not control traversal");
    Document order;
    check(order.readBytes("shape ( prim_states ( 0 ) points ( 0 ) )"),
          "dependency order unrestricted");
    Document unknown;
    check(unknown.readBytes("shape ( future LABEL ( word \"quoted\" nested ( 1 ) "
                            ") shape_header ( ff ) )"),
          "unknown text tree");
    auto text = unknown.encode(false, false, error);
    Document preserved;
    check(preserved.readBytes(text) && preserved.root.child("future")->label() == "LABEL" &&
              preserved.root.child("shape_header")->scalar(0) == "ff",
          "unknown label/scalars and known sibling retained");
    Document stringExtension;
    check(stringExtension.readBytes("shape ( images ( 1 image ( test.ace extension ( 1 ) ) ) )") &&
              stringExtension.root.child("images")->child("image")->scalar(0) == "test.ace" &&
              stringExtension.root.child("images")->child("image")->child("extension"),
          "unquoted string before an extension is not mistaken for a labeled "
          "block");
    const QByteArray spacedLabels =
        "shape model name ( matrices ( 2 "
        "matrix drzwi 1_002 ( 1 0 0 0 1 0 0 0 1 0 0 0 ) "
        "matrix \"drzwi 1_004\" ( 1 0 0 0 1 0 0 0 1 0 0 0 ) ) "
        "prim_states ( 1 prim_state material name ( 0 0 tex_idxs ( 0 ) 0 0 0 0 1 ) ) "
        "animations ( 1 animation ( 10 30 anim_nodes ( 1 "
        "anim_node drzwi 1_002 ( controllers ( 0 ) ) ) ) ) "
        "points ( 1 point arbitrary point name ( 1 2 3 ) ) )";
    Document labels;
    check(labels.readBytes(spacedLabels) && !labels.damaged && labels.diagnostics.size() == 5 &&
              labels.root.label() == "model name" &&
              labels.root.child("matrices").children("matrix")[0].label() == "drzwi 1_002" &&
              labels.root.child("matrices").children("matrix")[1].label() == "drzwi 1_004" &&
              labels.root.child("prim_states").child("prim_state").label() == "material name" &&
              labels.root.child("animations").child("animation").child("anim_nodes")
                  .child("anim_node").label() == "drzwi 1_002" &&
              labels.root.child("points").child("point").label() == "arbitrary point name",
          "recognized block headers recover complete names without a label-type whitelist");
    auto normalizedLabels = labels.encode(false, false, error);
    for (bool binary : {false, true}) {
        Document roundtrip;
        check(roundtrip.readBytes(labels.encode(binary, false, error)) && !roundtrip.damaged &&
                  roundtrip.diagnostics.empty() && roundtrip.encode(false, false, error) == normalizedLabels,
              "recovered labels normalize and survive text/binary save-reload");
    }
    Document compactLabels;
    check(compactLabels.readBytes(spacedLabels, false, true) && !compactLabels.damaged &&
              compactLabels.root.child("matrices").children("matrix")[0].label() == "drzwi 1_002",
          "Compact uses the same recovered matrix names");
    Document descriptor;
    check(descriptor.readBytes("SIMISA@@@@@@@@@@JINX0t1t______\n"
                              "shape ( train.s ESD_Detail_Level ( 0 ) )") && !descriptor.damaged &&
              descriptor.root.scalar(0) == "train.s" && descriptor.root.child("esd_detail_level"),
          "shape descriptor filename remains a value before a child block");
    for (auto input : {"matrix long label ) points ( 0 )", "matrix long label",
                       "matrix \"quoted name\" extra ( 0 )"}) {
        SimisTextReader reader(QString::fromLatin1(input));
        reader.next();
        QString label;
        bool recovered;
        check(!reader.readBlockHeader(label, recovered),
              "header recovery refuses close/EOF and suffix after a quoted label");
        if (QString::fromLatin1(input).contains("label )"))
            check(reader.peekKind() == SimisTextReader::Kind::Close,
                  "failed header leaves closing delimiter for enclosing reader");
    }
    // Dense numeric storage must preserve edits and binary output without one
    // heavyweight object per number. Unknown material is sparse and retained only
    // for Complete; Compact must never create those source-only records.
    const auto denseText = QByteArray("shape ( points ( 1000 ") +
                           QByteArray("point ( 1.25 -2.5 3.75 ) ").repeated(1000) +
                           ") colours ( 1000 " + QByteArray("colour ( 1 0 0 1 ) ").repeated(1000) +
                           ") future ( nested ( \"quoted ) text\" ) ) )";
    Document dense, compactDoc;
    check(dense.readBytes(denseText) && !dense.damaged && dense.storageBytes() < 200000,
          "dense arrays bound Complete storage for thousands of numeric values");
    check(compactDoc.readBytes(denseText, false, true) && !compactDoc.damaged &&
              compactDoc.compact && !compactDoc.root.child("colours") &&
              !compactDoc.root.child("future") && compactDoc.root.child("points").packed() &&
              compactDoc.root.child("points").packed()->rows() == 1000 &&
              compactDoc.root.child("points").packed()->number(1501) == -2.5f &&
              compactDoc.blockCount() == 2 && compactDoc.scalarCount() == 3001 &&
              compactDoc.storageBytes() < 20000 && compactDoc.skippedBlocks == 2,
          "Compact skips unneeded records/scalars during parsing");
    Document packedBinary, denseKnown;
    denseKnown.readBytes("shape ( )");
    denseKnown.root.appendCopy(dense.root.child("points"));
    check(packedBinary.readBytes(denseKnown.encode(true, false, error), false, true) &&
              !packedBinary.damaged && packedBinary.root.child("points").packed() &&
              packedBinary.root.child("points").packed()->words ==
                  compactDoc.root.child("points").packed()->words,
          "binary and text Compact tables retain identical native coordinates");
    // A late irregular row must restart the general parser without losing the
    // preceding rows; neither missing fields nor extensions may be discarded.
    for (const auto &row : {"point ( 4 5 )", "point ( 4 5 6 vector ( 7 8 9 ) )"}) {
        Document irregularSource;
        irregularSource.readBytes(QByteArray("shape ( points ( 999999999 point ( 1 2 3 ) ") + row +
                                  ") )");
        for (bool binary : {false, true}) {
            Document irregular;
            check(irregular.readBytes(irregularSource.encode(binary, false, error), false, true) &&
                      !irregular.root.child("points").packed() &&
                      irregular.root.child("points").children("point").size() == 2 &&
                      irregular.root.child("points").children("point")[0].number(2) == 3 &&
                      irregular.storageBytes() < 32768,
                  "irregular Compact table falls back without declared-count "
                  "allocation");
        }
    }
    compactDoc.encode(false, false, error);
    check(!error.isEmpty(), "Compact cannot serialize even before any GL upload");
    Document vertexSource;
    vertexSource.readBytes("shape ( vertices ( 2 vertex FIRST ( 00000000 7 8 ffffffff "
                           "ff000000 vertex_uvs ( 2 9 10 ) 0.5 ) "
                           "vertex ( 0 11 12 0 0 vertex_uvs ( 0 ) ) ) )");
    for (bool binary : {false, true}) {
        Document packedVertices;
        check(packedVertices.readBytes(vertexSource.encode(binary, false, error), false, true) &&
                  !packedVertices.damaged && packedVertices.root.child("vertices").packed() &&
                  packedVertices.root.child("vertices").packed()->words ==
                      std::vector<quint32>({7, 8, 9, 11, 12, 0xffffffffu}),
              "packed vertices preserve references, labels, multiple UVs and "
              "absent UVs");
    }
    Document copiedPacked = compactDoc;
    check(copiedPacked.root.child("points").packed()->words ==
                  compactDoc.root.child("points").packed()->words &&
              copiedPacked.root.child("points").packed() !=
                  compactDoc.root.child("points").packed(),
          "Compact document copies own their packed arrays");
    Document deepPacked;
    deepPacked.readBytes(QByteArray("shape ( ").repeated(128) + "points ( 1 point ( 1 2 3 ) )" +
                             QByteArray(" )").repeated(128),
                         false, true);
    check(deepPacked.damaged, "packed fast path respects the general nesting limit");
    auto denseCopy = dense;
    check(denseCopy.root.child("points").children("point")[500].setScalar(1, "7.5") &&
              dense.root.child("points").children("point")[500].number(1) == -2.5,
          "dense edit does not mutate an independent Complete document copy");
    Document denseReload;
    check(denseReload.readBytes(denseCopy.encode(false, false, error)) && error.isEmpty() &&
              denseReload.root.child("points").children("point")[500].number(1) == 7.5 &&
              denseReload.root.child("future").child("nested").scalar(0) == "quoted ) text",
          "dense point edit and unknown content survive save/reload");
    Document binaryCompact;
    check(binaryCompact.readBytes(canonical, false, true) && !binaryCompact.damaged &&
              binaryCompact.skippedBlocks > 0 && !binaryCompact.root.child("colours") &&
              binaryCompact.root.child("points"),
          "binary Compact skips before materializing source tables");
    auto withLargeIgnoredBlock = canonical;
    QByteArray ignored;
    word(ignored, 0xf1234567u);
    word(ignored, 1024 * 1024 + 1);
    ignored.append('\0');
    ignored.append(QByteArray(1024 * 1024, 'x'));
    qToLittleEndian(qFromLittleEndian<quint32>(withLargeIgnoredBlock.constData() + 36) +
                        quint32(ignored.size()),
                    withLargeIgnoredBlock.data() + 36);
    withLargeIgnoredBlock += ignored;
    Document skipLarge;
    check(skipLarge.readBytes(withLargeIgnoredBlock, false, true) && !skipLarge.damaged &&
              skipLarge.storageBytes() < 32768,
          "Compact allocation estimate excludes a large ignored binary block");
    qToLittleEndian<quint32>(0xffffffffu, withLargeIgnoredBlock.data() + 36);
    Document invalidCompactRoot;
    check(!invalidCompactRoot.readBytes(withLargeIgnoredBlock, false, true) &&
              invalidCompactRoot.damaged,
          "Compact allocation prepass bounds invalid root lengths");
    using R = SimisTextReader;
    {
        Document arrays;
        check(arrays.readBytes("shape ( hierarchy ( 3 -1 0 1 ) flags ( 2 ffffffff 01234567 ) "
                               "vertex_idxs ( 3 0 1 2 ) )") &&
                  arrays.root.child("hierarchy").integer(1) == -1 &&
                  arrays.root.child("flags").scalar(1) == "ffffffff" &&
                  arrays.root.child("vertex_idxs").integer(3) == 2,
              "direct arrays preserve signed, hex and unsigned native values");
        Document reloadArrays;
        const auto encoded = arrays.encode(true, false, error);
        check(error.isEmpty() && reloadArrays.readBytes(encoded) &&
                  reloadArrays.encode(true, false, error) == encoded,
              "direct-array values survive canonical binary save/reload");
        for (bool compact : {false, true}) {
            Document fallback;
            check(fallback.readBytes("shape ( vertex_idxs ( 999999999 1 bad point ( 2 3 4 ) 5 ) "
                                     "hierarchy ( 2 -1 0 ) )", false, compact) &&
                      fallback.root.child("vertex_idxs").scalarCount() == 4 &&
                      fallback.root.child("vertex_idxs").scalar(2) == "bad" &&
                      fallback.root.child("vertex_idxs").child("point").number(2) == 4 &&
                      fallback.root.child("hierarchy").integer(1) == -1 &&
                      fallback.storageBytes() < 32768,
                  "array fallback rolls back appended words and retains irregular fields");
        }
        Document incompleteArray;
        incompleteArray.readBytes("shape ( vertex_idxs ( 3 1 2");
        check(incompleteArray.damaged &&
                  incompleteArray.root.child("vertex_idxs").scalarCount() == 3,
              "unterminated fast array retains partial values through general parsing");
    }
    {
        QString input;
        for (int i = 0; i < 30; ++i)
            input += QString::number(i) + ' ';
        R reader(input);
        bool equal = true;
        for (int i = 0; i < 30; ++i) {
            const auto expectedPosition = reader.position();
            reader.peek(2); // Exercise queue wraparound and repeated End lookahead.
            R snapshot = reader;
            equal &= reader.position() == snapshot.position() &&
                     reader.position() >= expectedPosition && reader.peekKind() == R::Kind::Atom;
            double number = -1;
            equal &= snapshot.readNumber(number) && number == i &&
                     reader.next().text == QString::number(i);
        }
        equal &= reader.peekKind() == R::Kind::End && reader.peekKind(3) == R::Kind::Error &&
                 reader.peek(-1).kind == R::Kind::Error;
        check(equal, "lookahead ring wraps and reader snapshots advance independently");
    }
    {
        R reader("( nested ( \"a)b\" + \"c\" ) value ) tail");
        reader.next();
        reader.peek(2);
        R snapshot = reader;
        check(reader.skipBlock() && reader.next().text == "tail" && snapshot.skipBlock() &&
                  snapshot.next().text == "tail",
              "skipping queued nested tokens and quoted delimiters preserves "
              "snapshots");
    }
    const QStringList numericCases = {"0",
                                      "+.5",
                                      "-1.25e+3",
                                      "1e-20",
                                      "3.4028234663852886e+38",
                                      "1e309",
                                      "1e-999",
                                      "nan",
                                      "inf",
                                      "+-1",
                                      "1e+",
                                      "1,000",
                                      "1x",
                                      "0xffffffff",
                                      "4294967295",
                                      "4294967296",
                                      "0x100000000",
                                      "-2147483648",
                                      "-0",
                                      "( ",
                                      ") ",
                                      "\"12\"",
                                      "\"1\" + \"2\"",
                                      QString(150, '0') + "1.25",
                                      QString::fromUtf8("\xe2\x80\x83") + "1.5",
                                      QString(QChar(0xfeff)) + "2.5",
                                      "1" + QString(QChar(0xfeff)) + "2"};
    bool conversionsEqual = true;
    for (const auto &literal : numericCases)
        for (int ahead : {-1, 0, 2}) {
            R tokens(literal + " tail"), direct(literal + " tail");
            if (ahead >= 0) {
                tokens.peek(ahead);
                direct.peekKind(ahead);
            }
            double oldValue = 0, newValue = 0;
            const bool oldOk = R::number(tokens.next(), oldValue),
                       newOk = direct.readNumber(newValue);
            conversionsEqual &= oldOk == newOk && (!oldOk || oldValue == newValue) &&
                                tokens.position() == direct.position() &&
                                tokens.next().text == direct.next().text;
            for (int base : {2, 10, 16}) {
                R a(literal + " tail"), b(literal + " tail");
                if (ahead >= 0) {
                    a.peek(ahead);
                    b.peekKind(ahead);
                }
                quint32 av = 0, bv = 0;
                const bool ao = R::unsignedInteger(a.next(), av, base),
                           bo = b.readUnsignedInteger(bv, base);
                conversionsEqual &= ao == bo && (!ao || av == bv) && a.position() == b.position();
            }
        }
    check(conversionsEqual, "direct numeric reads match token conversion and cursor consumption");
    R signedDirect("-2147483648 2147483647 -2147483649 2147483648");
    qint32 signedDirectValue;
    check(signedDirect.readInteger(signedDirectValue) && signedDirectValue == -2147483647 - 1 &&
              signedDirect.readInteger(signedDirectValue) && signedDirectValue == 2147483647 &&
              !signedDirect.readInteger(signedDirectValue) &&
              !signedDirect.readInteger(signedDirectValue),
          "direct signed boundaries reject overflow");
    R hexLimit("0xffffffff 0x100000000 ffffffff 100000000 -1");
    quint32 hexValue;
    check(hexLimit.readUnsignedInteger(hexValue, 16) && hexValue == 0xffffffffu &&
              !hexLimit.readUnsignedInteger(hexValue, 16) &&
              hexLimit.readUnsignedInteger(hexValue, 16) && hexValue == 0xffffffffu &&
              !hexLimit.readUnsignedInteger(hexValue, 16) &&
              !hexLimit.readUnsignedInteger(hexValue, 16),
          "direct hexadecimal boundaries reject overflow and negative values");
    for (const auto &literal : {"+-1", "1e+", "nan", "inf", "1,000", "1x", "1e999"}) {
        R r(literal);
        double value;
        check(!R::number(r.next(), value), "invalid numeric token rejected: " + QString(literal));
    }
    for (const auto &literal : {"+.5", "-1.25e+3", "1e-20", "3.4028234663852886e+38"}) {
        R r(literal);
        double value;
        check(R::number(r.next(), value), "finite numeric token accepted: " + QString(literal));
    }
    R signedLimit("-2147483648 2147483648"), unsignedLimit("4294967295 4294967296");
    qint32 signedValue;
    quint32 unsignedValue;
    check(R::integer(signedLimit.next(), signedValue) &&
              signedValue == std::numeric_limits<qint32>::min() &&
              !R::integer(signedLimit.next(), signedValue),
          "signed boundaries checked");
    check(R::unsignedInteger(unsignedLimit.next(), unsignedValue) && unsignedValue == 0xffffffffu &&
              !R::unsignedInteger(unsignedLimit.next(), unsignedValue),
          "unsigned boundaries checked");
    QString decoded;
    check(!SimisTextReader::decode(QByteArray("\xff\xfeX", 3), decoded, error),
          "odd UTF16 rejected");
    std::cout << "Shape document checks " << checks << " failures " << failures << std::endl;
    return failures ? 1 : 0;
}
