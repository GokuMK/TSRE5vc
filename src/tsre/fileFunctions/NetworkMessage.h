/*  This file is part of TSRE5.
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef NETWORKMESSAGE_H
#define NETWORKMESSAGE_H

#include <QByteArray>
#include <QString>

class FileBuffer;

// The server and client mode's messages (task server 02). Every WebSocket
// message is one frame: a header and a payload. The payload is UTF-16 text
// with its byte order mark, of ParserX records, or a binary record ('B' and
// a NetworkToken).
//
// The header, little endian, 20 bytes:
//   4  the mark "TSRN"
//   2  the protocol version
//   1  the kind: 1 text, 2 binary
//   1  flags: bit 0 the payload is compressed (qCompress)
//   4  the request number: the sender's; a reply carries its request's;
//      0 asks for no reply
//   4  the change number: the server's, on a change it forwards; else 0
//   4  the payload's length, as it follows the header
namespace NetworkMessage {
const quint16 Protocol = 1;
const int HeaderSize = 20;
// The largest message a server takes from a client (an edit record or a
// terrain tile), and a client from the server (PRUDNIK's TDB is 20 MB of
// UTF-16), uncompressed.
const qint64 ClientLimit = 8ll * 1024 * 1024;
const qint64 ServerLimit = 256ll * 1024 * 1024;
// Payloads larger than this are compressed.
const int CompressAbove = 16 * 1024;

enum Kind : quint8 { Text = 1, Binary = 2 };
enum Flag : quint8 { Compressed = 1 };

struct Frame {
    quint16 protocol = Protocol;
    quint8 kind = Text;
    quint8 flags = 0;
    quint32 request = 0;
    quint32 change = 0;
    // Uncompressed.
    QByteArray payload;
};

enum class Read { Ok, NotFrame, OtherProtocol, TooLarge, Malformed };

// A text as a payload: UTF-16 with its byte order mark.
QByteArray utf16(const QString &text);
// A payload as a frame, its kind from its first bytes; compressed when large.
QByteArray frame(const QByteArray &payload, quint32 request = 0, quint32 change = 0);
QByteArray textFrame(const QString &text, quint32 request = 0, quint32 change = 0);
// A message read as a frame. Its payload at most `limit` bytes uncompressed:
// a compressed payload stating more is not uncompressed. `error` says why it
// is not read.
Read readFrame(const QByteArray &message, qint64 limit, Frame &out, QString *error = nullptr);
// A frame's protocol version, if it is a frame of any version.
bool frameProtocol(const QByteArray &message, quint16 &protocol);

// The login's answer to the server's challenge: HMAC-SHA256 keyed by the
// password, over the challenge's nonce and the user name, in hex. The
// password itself never crosses the wire.
QString loginAnswer(const QString &password, const QByteArray &nonce, const QString &user);
// A new random nonce (32 bytes).
QByteArray newNonce();

// A copy of a payload's bytes for the parser; the buffer owns it, so it is
// freed with the buffer.
FileBuffer *buffer(const QByteArray &message);
// After a record's contents: past its closing bracket, also when that
// bracket is the message's last character (ParserX::SkipToken stops short of
// it there).
void skipRecord(FileBuffer *data);
// Part of a buffer, from `start` to `end`, as a buffer of its own (a
// record's contents: its parser stops at their end).
FileBuffer *part(const FileBuffer *data, int start, int end);
// One record of a text payload, from before its name to after its closing
// bracket, as a payload of its own.
QByteArray record(const FileBuffer *data, int start, int end);
// A user name as the server shows it: letters and digits, at most 12, never
// empty.
QString cleanUsername(const QString &name);
// A text for a record's quoted string: no double quotes in it.
QString quoted(QString text);
}

#endif /* NETWORKMESSAGE_H */
