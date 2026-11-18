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

// The server and client mode's messages (binary WebSocket frames): UTF-16
// text with its byte order mark, of ParserX records, or 'B' and a binary
// record (NetworkToken).
namespace NetworkMessage {
// A text as a message: UTF-16 with its byte order mark.
QByteArray utf16(const QString &text);
// A copy of a message's bytes for the parser; the buffer owns it, so it is
// freed with the buffer.
FileBuffer *buffer(const QByteArray &message);
// After a record's contents: past its closing bracket, also when that
// bracket is the message's last character (ParserX::SkipToken stops short of
// it there).
void skipRecord(FileBuffer *data);
// Part of a buffer, from `start` to `end`, as a buffer of its own (a
// record's contents: its parser stops at their end).
FileBuffer *part(const FileBuffer *data, int start, int end);
// One record of a text message, from before its name to after its closing
// bracket, as a message of its own.
QByteArray record(const FileBuffer *data, int start, int end);
// A user name as the server shows it: letters and digits, at most 12, never
// empty.
QString cleanUsername(const QString &name);
}

#endif /* NETWORKMESSAGE_H */
