/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors. 
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later. 
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#ifndef SIGCFG_H
#define	SIGCFG_H

#include <QString>
#include <QStringList>
#include <QHash>

class SignalShape;
class SignalType;

// The route's sigcfg.dat. Everything Open Rails reads is kept; names of
// signal types, lights, light textures and table entries are lower case.
class SigCfg {
public:
    struct LightTexture {
        QString name;
        QString file;
        float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
    };
    struct LightColour {
        QString name;
        unsigned char a = 255, r = 255, g = 255, b = 255;
    };

    QHash<QString, LightTexture> lightTextures;
    QHash<QString, LightColour> lightsTable;
    QHash<QString, SignalShape*> signalShape;
    QHash<int, SignalShape*> signalShapeById;
    QHash<QString, SignalType*> signalType;
    // ORTSSignalFunctions: Open Rails function name to its MSTS function.
    QHash<QString, QString> signalFunctions;
    // ORTSNormalSubtypes, upper case.
    QStringList normalSubtypes;
    QStringList scriptFiles;
    bool loaded = false;
    bool sourceFileExists = false;
    // Problems met while reading; the blocks concerned were skipped.
    QStringList warnings;

    // The current route's sigcfg.dat.
    SigCfg();
    explicit SigCfg(const QString &path);
    virtual ~SigCfg();
    SignalShape* findSignalShape(const QString &fileName) const;
    const SignalType* findSignalType(const QString &name) const;
private:
    void load(const QString &path);
};

#endif	/* SIGCFG_H */
