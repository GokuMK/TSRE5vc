/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef MSTSTEXTFILEVALIDATION_H
#define MSTSTEXTFILEVALIDATION_H

#include <QString>

#include <tsre/fileFunctions/FileBuffer.h>

namespace MstsTextFileValidation {

inline bool validate(FileBuffer *buffer, QString &error) {
    if(buffer == NULL || buffer->data == NULL || buffer->length < 2
            || (buffer->length % 2) != 0) {
        error = "file has no valid UTF-16 payload";
        return false;
    }

    QString text;
    text.reserve(buffer->length / 2);
    int offset = 0;
    if(buffer->length >= 2
            && (buffer->data[0] | (buffer->data[1] << 8)) == 0xfeff)
        offset = 2;
    for(int i = offset; i + 1 < buffer->length; i += 2) {
        const ushort value = static_cast<ushort>(buffer->data[i])
                | (static_cast<ushort>(buffer->data[i + 1]) << 8);
        text.append(QChar(value));
    }

    if(!text.trimmed().startsWith("SIMISA", Qt::CaseInsensitive)) {
        error = "file has no SIMISA header";
        return false;
    }

    int depth = 0;
    bool quoted = false;
    bool escaped = false;
    for(const QChar ch : text) {
        if(quoted) {
            if(escaped) {
                escaped = false;
            } else if(ch == '\\') {
                escaped = true;
            } else if(ch == '"') {
                quoted = false;
            }
            continue;
        }
        if(ch == '"') {
            quoted = true;
        } else if(ch == '(') {
            ++depth;
        } else if(ch == ')') {
            if(--depth < 0) {
                error = "file has an unmatched closing parenthesis";
                return false;
            }
        }
    }

    if(quoted) {
        error = "file has an unterminated quoted string";
        return false;
    }
    if(depth != 0) {
        error = "file has unbalanced parentheses";
        return false;
    }
    return true;
}

} // namespace MstsTextFileValidation

#endif // MSTSTEXTFILEVALIDATION_H
