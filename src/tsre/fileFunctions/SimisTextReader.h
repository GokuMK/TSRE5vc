#pragma once
#include <QByteArray>
#include <QString>
#include <QStringView>
#include <QVector>
#include <array>

// Bounded SIMIS text lexer. No shape, document-tree, or graphics dependencies.
// Offsets refer to UTF-16 code units in the decoded input.
class SimisTextReader {
  public:
    enum class Kind { Atom, String, Open, Close, End, Error };
    struct Token {
        Kind kind = Kind::End;
        // Ordinary atoms borrow the reader's immutable source. Copy explicitly
        // with QString(text.constData(), text.size()) to retain beyond its
        // lifetime. Decoded quoted strings own their storage.
        QString text;
        qsizetype begin = 0, end = 0;
    };
    struct Diagnostic {
        qsizetype offset;
        int line, column;
        QString message;
    };
    explicit SimisTextReader(QString source);
    // Reads from `start` on (offsets still count from the text's start).
    SimisTextReader(QString source, qsizetype start);
    Token next();
    Token peek(int ahead = 0);
    Kind peekKind(int ahead = 0); // Lookahead without copying token text.
    // Consume one token, using the same checked conversions as the Token APIs.
    bool readNumber(double &value);
    bool readInteger(qint32 &value);
    bool readUnsignedInteger(quint32 &value, int base = 10);
    // Caller has already identified/consumed a block keyword. Consumes its '('.
    // Recovery joins unquoted label words; never crosses ')' or EOF.
    bool readBlockHeader(QString &label, bool &recovered);
    bool skipBlock(); // Called immediately after '('; stops after matching ')'.
    qsizetype position() const;
    QString slice(qsizetype begin, qsizetype end) const;
    const QVector<Diagnostic> &diagnostics() const { return errors; }
    // A text file's bytes as every SIMIS text reader takes them (the
    // encodings agreed for task driving 01): UTF-16 LE or BE with a byte
    // order mark; UTF-8 with or without one (ASCII is UTF-8); UTF-16
    // without a byte order mark, told by zero bytes among its first
    // characters, and anything else as Latin-1, each with a `note` saying
    // so. False only for broken UTF-16.
    static bool decode(const QByteArray &bytes, QString &out, QString &error, QString *note = nullptr);
    static bool number(const Token &, double &value);
    static bool integer(const Token &, qint32 &value);
    static bool unsignedInteger(const Token &, quint32 &value, int base = 10);
    static QString quote(const QString &);

  private:
    QString source;
    qsizetype cursor = 0;
    // Lookahead is bounded to three tokens. A ring avoids allocations and
    // front-removal shifts, and reader snapshots own their queue state.
    std::array<Token, 3> pending;
    int pendingHead = 0, pendingCount = 0;
    QVector<Diagnostic> errors;
    Token scan();
    const Token &lookahead(int ahead);
    QStringView nextAtom();
    Token failure(qsizetype start, const QString &message);
};
