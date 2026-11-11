/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef OGGFILE_H
#define OGGFILE_H

#include <tsre/sound/WavFile.h>

#include <QByteArray>
#include <QString>

// Ogg Vorbis sound files (MaSzyna's, task maszyna 05), decoded by
// stb_vorbis into the same WavData a wave file gives: interleaved 16-bit
// samples at the file's rate, without loop points.
namespace OggFile {

// The bytes start as an Ogg stream does ("OggS").
bool isOgg(const QByteArray &bytes);
// Decodes one or two channels at any rate. False with a reason for
// anything else (not Vorbis, more channels, broken or empty data).
bool parse(const QByteArray &bytes, WavData &out, QString *error = nullptr);
bool load(const QString &path, WavData &out, QString *error = nullptr);

}

#endif
