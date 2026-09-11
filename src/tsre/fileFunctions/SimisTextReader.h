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
    static bool decode(const QByteArray &bytes, QString &out, QString &error);
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
