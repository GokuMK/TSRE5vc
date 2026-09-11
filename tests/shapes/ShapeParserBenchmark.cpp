#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <tsre/fileFunctions/ParserX.h>
#include <tsre/fileFunctions/SimisTextReader.h>
#include <tsre/shape/SFileDocument.h>
#define MINIZ_NO_ZLIB_COMPATIBLE_NAMES
#include <mzip/miniz/miniz.h>

using SFileDetail::Document;
using SFileDetail::Node;
static void emitResult(const QJsonObject &o) {
    std::cout << QJsonDocument(o).toJson(QJsonDocument::Compact).constData() << std::endl;
}
template <class Prepare, class Run> static QJsonObject measure(Prepare prepare, Run run) {
    QJsonArray samples;
    std::vector<double> times;
    for (int i = -1; i < 7; ++i) {
        prepare(); // Includes destruction of the preceding result, outside timing.
        QElapsedTimer timer;
        timer.start();
        run();
        double ms = timer.nsecsElapsed() / 1e6;
        if (i >= 0) {
            samples.append(ms);
            times.push_back(ms);
        }
    }
    std::sort(times.begin(), times.end());
    return {{"median_ms", times[times.size() / 2]}, {"samples_ms", samples}};
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        auto args = app.arguments();
        args.removeFirst();
        const bool compact = args.removeOne("--compact");
        QString exportDir;
        if (args.size() >= 2 && args[0] == "--export-text") {
            args.removeFirst();
            exportDir = args.takeFirst();
            if (!QDir().mkpath(exportDir))
                throw std::runtime_error("create export directory");
        }
        for (const auto &path : args) {
            std::unique_ptr<Document> doc;
            auto result = measure(
                [&] {
                    doc.reset();
                    doc = std::make_unique<Document>();
                },
                [&] {
                    if (!doc->read(path, false, compact) || doc->damaged)
                        throw std::runtime_error(doc->diagnostics.join("; ").toStdString());
                });
            qint64 blocks = doc->blockCount(), scalars = doc->scalarCount();
            result["path"] = path;
            result["stage"] = "document_read";
            result["blocks"] = blocks;
            result["scalars"] = scalars;
            result["node_size"] = int(Document::recordBytes());
            result["document_bytes"] = double(doc->storageBytes());
            result["compact"] = compact;
            result["skipped_blocks"] = double(doc->skippedBlocks);
            result["binary"] = doc->binary;
            result["compressed"] = doc->compressed;
            emitResult(result);
            if (!exportDir.isEmpty()) {
                QString error;
                if (!doc->save(exportDir + "/" + QFileInfo(path).fileName(), false, false, error))
                    throw std::runtime_error(error.toStdString());
            }
            const bool binary = doc->binary;
            doc.reset();
            if (binary)
                continue;
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                throw std::runtime_error("open input");
            const auto bytes = f.readAll();
            QString decoded, error;
            auto decode = measure([&] { decoded.clear(); },
                                  [&] {
                                      if (!SimisTextReader::decode(bytes, decoded, error))
                                          throw std::runtime_error(error.toStdString());
                                  });
            decode["path"] = path;
            decode["stage"] = "utf16_decode";
            emitResult(decode);
            std::unique_ptr<SimisTextReader> reader;
            qint64 tokens = 0;
            auto lex = measure(
                [&] {
                    reader = std::make_unique<SimisTextReader>(decoded);
                    tokens = 0;
                },
                [&] {
                    for (;;) {
                        auto t = reader->next();
                        if (t.kind == SimisTextReader::Kind::End)
                            break;
                        if (t.kind == SimisTextReader::Kind::Error)
                            throw std::runtime_error("lexer failure");
                        ++tokens;
                    }
                });
            lex["path"] = path;
            lex["stage"] = "lexer_only";
            lex["tokens"] = double(tokens);
            emitResult(lex);
            auto lookahead = measure(
                [&] { reader = std::make_unique<SimisTextReader>(decoded); },
                [&] {
                    qint64 count = 0;
                    while (reader->peek().kind != SimisTextReader::Kind::End) {
                        if (reader->next().kind == SimisTextReader::Kind::Error)
                            throw std::runtime_error("lookahead lexer failure");
                        ++count;
                    }
                    if (count != tokens)
                        throw std::runtime_error("lookahead token count mismatch");
                });
            lookahead["path"] = path;
            lookahead["stage"] = "lexer_with_lookahead";
            emitResult(lookahead);
            auto kindLookahead = measure(
                [&] { reader = std::make_unique<SimisTextReader>(decoded); }, [&] {
                    qint64 count = 0;
                    while (reader->peekKind() != SimisTextReader::Kind::End) {
                        if (reader->next().kind == SimisTextReader::Kind::Error)
                            throw std::runtime_error("kind lookahead lexer failure");
                        ++count;
                    }
                    if (count != tokens)
                        throw std::runtime_error("kind lookahead token count mismatch");
                });
            kindLookahead["path"] = path;
            kindLookahead["stage"] = "lexer_with_kind_lookahead";
            emitResult(kindLookahead);
            // Retain borrowed tokens while decoded stays alive. Selection and
            // tokenization happen outside timing; only conversion is timed.
            std::vector<SimisTextReader::Token> numericTokens;
            SimisTextReader collect(decoded);
            double expectedSum = 0, convertedSum = 0;
            for (;;) {
                auto token = collect.next();
                if (token.kind == SimisTextReader::Kind::End)
                    break;
                double number;
                if (SimisTextReader::number(token, number)) {
                    numericTokens.push_back(std::move(token));
                    expectedSum += number;
                }
            }
            auto conversion = measure([&] { convertedSum = 0; }, [&] {
                for (const auto &token : numericTokens) {
                    double number;
                    if (!SimisTextReader::number(token, number))
                        throw std::runtime_error("numeric conversion failure");
                    convertedSum += number;
                }
                if (convertedSum != expectedSum)
                    throw std::runtime_error("numeric conversion checksum mismatch");
            });
            conversion["path"] = path;
            conversion["stage"] = "numeric_conversion_only";
            conversion["numbers"] = double(numericTokens.size());
            conversion["checksum"] = convertedSum;
            emitResult(conversion);
        }
        // Exactly representable literals: checksums must match despite float/double
        // APIs.
        const QString numbers = QString("0.5 -1.5 123.5 0 10.5 ").repeated(20000);
        constexpr int count = 100000;
        double newSum = 0, oldSum = 0;
        std::unique_ptr<SimisTextReader> reader;
        auto modern = measure(
            [&] {
                reader = std::make_unique<SimisTextReader>(numbers);
                newSum = 0;
            },
            [&] {
                for (int i = 0; i < count; ++i) {
                    double x;
                    if (!SimisTextReader::number(reader->next(), x))
                        throw std::runtime_error("number failure");
                    newSum += x;
                }
            });
        std::unique_ptr<FileBuffer> buffer;
        auto legacy = measure(
            [&] {
                auto bytes = new unsigned char[numbers.size() * 2];
                std::memcpy(bytes, numbers.utf16(), numbers.size() * 2);
                buffer = std::make_unique<FileBuffer>(bytes, numbers.size() * 2);
                oldSum = 0;
            },
            [&] {
                for (int i = 0; i < count; ++i)
                    oldSum += ParserX::GetNumber(buffer.get());
            });
        if (oldSum != newSum || newSum != 2660000.0)
            throw std::runtime_error("numeric checksum mismatch");
        modern["stage"] = "simis_numeric_100k";
        modern["checksum"] = newSum;
        emitResult(modern);
        auto direct = measure(
            [&] {
                reader = std::make_unique<SimisTextReader>(numbers);
                newSum = 0;
            }, [&] {
                for (int i = 0; i < count; ++i) {
                    double x;
                    if (!reader->readNumber(x))
                        throw std::runtime_error("direct number failure");
                    newSum += x;
                }
                if (newSum != oldSum)
                    throw std::runtime_error("direct numeric checksum mismatch");
            });
        direct["stage"] = "simis_direct_numeric_100k";
        direct["checksum"] = newSum;
        emitResult(direct);
        legacy["stage"] = "parserx_numeric_100k";
        legacy["checksum"] = oldSum;
        emitResult(legacy);
    } catch (const std::exception &e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
}
