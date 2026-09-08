/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef ACELIBLEGACY_H
#define ACELIBLEGACY_H

#include <QThread>
#include <QImage>
#include <tsre/texture/Texture.h>

class AceLibLegacy : public QThread
 {
     Q_OBJECT

public:
    static bool IsThread;
    AceLibLegacy();
    //static bool LoadACE(Texture* texture);
    Texture* texture;
    static void save(QString path, Texture* t);
    // Checked RGB format-14 output; legacy save/reader remain reference paths.
    static bool saveRgbChecked(const QString &path, const QImage &image, QString &error);
    void run();
private:

protected:

};

#endif	/* ACELIB_H */
