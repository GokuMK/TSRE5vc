/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

#include <tsre/sound/SoundCsv.h>

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <cmath>
#include <memory>

namespace {

struct File {
    QFile file;
    QTextStream out;
};

struct Csv {
    QString folder;
    bool on = false;
    double stateSecond = -1.0;
    std::unique_ptr<File> sounds, states, marks;
};

Csv &csv() {
    static Csv c = [] {
        Csv made;
        made.folder = qEnvironmentVariable("TSRE_SOUND_CSV");
        made.on = !made.folder.isEmpty() && QDir().mkpath(made.folder);
        if (!made.folder.isEmpty() && !made.on)
            qWarning().noquote() << "Sound: no folder for TSRE_SOUND_CSV" << made.folder;
        return made;
    }();
    return c;
}

QTextStream *open(std::unique_ptr<File> &f, const char *name, const char *columns) {
    Csv &c = csv();
    if (!c.on)
        return nullptr;
    if (f == nullptr) {
        f = std::make_unique<File>();
        f->file.setFileName(QDir(c.folder).filePath(QString::fromLatin1(name)));
        if (!f->file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            qWarning().noquote() << "Sound: cannot write" << f->file.fileName();
            c.on = false;
            return nullptr;
        }
        f->out.setDevice(&f->file);
        f->out << columns << '\n';
    }
    return &f->out;
}

QString number(float v, int decimals) {
    return std::isfinite(v) ? QString::number(double(v), 'f', decimals) : QString();
}

// A field quoted when it must be.
QString field(const QString &s) {
    if (!s.contains(QLatin1Char(',')) && !s.contains(QLatin1Char('"')) && !s.contains(QLatin1Char('\n')))
        return s;
    QString q = s;
    q.replace(QLatin1String("\""), QLatin1String("\"\""));
    return QLatin1Char('"') + q + QLatin1Char('"');
}

}

namespace SoundCsv {

bool enabled() {
    return csv().on;
}

void flush() {
    Csv &c = csv();
    for (const auto *f : {&c.sounds, &c.states, &c.marks})
        if (*f != nullptr)
            (*f)->out.flush();
}

void sound(double t, const QString &key, const QString &file, const float *place, float gain, float pitch,
           float soundproofing, float distance, float final) {
    QTextStream *out = open(csv().sounds, "sounds.csv", "t,key,file,x,y,z,gain,pitch,soundproofing,distance,final");
    if (out == nullptr)
        return;
    QString name = file;
    name.replace(QLatin1Char('\\'), QLatin1Char('/'));
    *out << QString::number(t, 'f', 0) << ',' << field(key) << ',' << field(name.toLower()) << ','
         << (place != nullptr ? number(place[0], 2) : QString()) << ','
         << (place != nullptr ? number(place[1], 2) : QString()) << ','
         << (place != nullptr ? number(place[2], 2) : QString()) << ',' << number(gain, 4) << ','
         << number(pitch, 4) << ',' << number(soundproofing, 4) << ',' << number(distance, 4) << ','
         << number(final, 4) << '\n';
}

bool takeState(double t) {
    Csv &c = csv();
    if (!c.on || t < 0.0 || t <= c.stateSecond)
        return false;
    c.stateSecond = t;
    return true;
}

void state(double t, const QString &name, const QString &value) {
    QTextStream *out = open(csv().states, "state.csv", "t,name,value");
    if (out != nullptr)
        *out << QString::number(t, 'f', 0) << ',' << field(name) << ',' << field(value) << '\n';
}

void mark(double t, double sim, const QString &event) {
    QTextStream *out = open(csv().marks, "marks.csv", "t,sim,event");
    if (out == nullptr)
        return;
    *out << QString::number(t, 'f', 2) << ',' << QString::number(sim, 'f', 2) << ',' << field(event) << '\n';
    out->flush();
}

}
