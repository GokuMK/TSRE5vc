/*  This file is part of TSRE5.
 *
 *  TSRE5 - train sim game engine and MSTS/OR Editors.
 *  Copyright (C) 2016 Piotr Gadecki <pgadecki@gmail.com>
 *
 *  Licensed under GNU General Public License 3.0 or later.
 *
 *  See LICENSE.md or https://www.gnu.org/licenses/gpl.html
 */

// RmlUi's font engine on Qt's fonts (task scripting 01): the functions of
// RmlUi's FreeType interface (thirdparty/rmlui, FreeTypeInterface.h) written
// with QRawFont, so TSRE needs no FreeType library. Built into tsre_rmlui in
// place of RmlUi's own file. Glyphs are filled from their outlines by Qt's
// rasteriser: the same pixels on every system, without hinting. One face a
// file (the first of a collection), no variable font instances; kerning from
// the font's 'kern' table only (Qt applies no GPOS kerning to a pair).

#include <FontEngineDefault/FreeTypeInterface.h>
#include <RmlUi/Core/Log.h>

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QRawFont>
#include <cmath>
#include <cstring>

namespace Rml {

namespace {

struct Face {
    QByteArray data;
    // At the size last set.
    QRawFont font;
    int size = 0;
};

Face *faceOf(FontFaceHandleFreetype handle) {
    return reinterpret_cast<Face *>(handle);
}

bool setSize(Face *face, int size) {
    if (size > 0 && face->size != size) {
        face->font.setPixelSize(size);
        face->size = size;
    }
    return face->font.isValid();
}

quint32 glyphIndex(const QRawFont &font, Character character) {
    const char32_t code = char32_t(character);
    if (!font.supportsCharacter(uint(code)))
        return 0;
    const QList<quint32> indexes = font.glyphIndexesForString(QString::fromUcs4(&code, 1));
    return indexes.isEmpty() ? 0 : indexes.first();
}

bool buildGlyph(const QRawFont &font, Character character, FontGlyphMap &glyphs) {
    const quint32 index = glyphIndex(font, character);
    if (index == 0)
        return false;
    auto result = glyphs.emplace(character, FontGlyph{});
    if (!result.second)
        return false;
    FontGlyph &glyph = result.first->second;
    const QList<QPointF> advances = font.advancesForGlyphIndexes(QList<quint32>{index});
    glyph.advance = advances.isEmpty() ? 0 : qRound(advances.first().x());

    const QPainterPath path = font.pathForGlyph(index);
    const QRectF bounds = path.boundingRect();
    if (path.isEmpty() || bounds.width() <= 0 || bounds.height() <= 0)
        return true;
    // Whole pixels around the outline; y grows down from the baseline.
    const int left = int(std::floor(bounds.left()));
    const int top = int(std::floor(bounds.top()));
    const int width = int(std::ceil(bounds.right())) - left;
    const int height = int(std::ceil(bounds.bottom())) - top;
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.translate(-left, -top);
    painter.fillPath(path, Qt::white);
    painter.end();

    glyph.bearing = Vector2i(left, -top);
    glyph.bitmap_dimensions = Vector2i(width, height);
    glyph.color_format = ColorFormat::A8;
    glyph.bitmap_owned_data.reset(new byte[size_t(width) * size_t(height)]);
    for (int y = 0; y < height; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        byte *alpha = glyph.bitmap_owned_data.get() + size_t(y) * size_t(width);
        for (int x = 0; x < width; ++x)
            alpha[x] = byte(qAlpha(line[x]));
    }
    glyph.bitmap_data = glyph.bitmap_owned_data.get();
    return true;
}

// A box outline for characters the face does not have (as RmlUi's own).
void addReplacement(int size, FontGlyphMap &glyphs) {
    if (glyphs.find(Character::Replacement) != glyphs.end())
        return;
    FontGlyph glyph;
    glyph.bitmap_dimensions = {size / 3, (size * 2) / 3};
    glyph.advance = glyph.bitmap_dimensions.x + 2;
    glyph.bearing = {1, glyph.bitmap_dimensions.y};
    glyph.bitmap_owned_data.reset(new byte[size_t(glyph.bitmap_dimensions.x) * size_t(glyph.bitmap_dimensions.y)]);
    glyph.bitmap_data = glyph.bitmap_owned_data.get();
    for (int y = 0; y < glyph.bitmap_dimensions.y; ++y) {
        for (int x = 0; x < glyph.bitmap_dimensions.x; ++x) {
            const bool edge = x < 1 || x >= glyph.bitmap_dimensions.x - 1 || y < 1 || y >= glyph.bitmap_dimensions.y - 1;
            glyph.bitmap_owned_data[size_t(y * glyph.bitmap_dimensions.x + x)] = edge ? 0xdd : 0;
        }
    }
    glyphs[Character::Replacement] = std::move(glyph);
}

}

bool FreeType::Initialise() {
    return true;
}

void FreeType::Shutdown() {
}

bool FreeType::GetFaceVariations(Span<const byte> data, Vector<FaceVariation> &, int) {
    const QRawFont font(QByteArray(reinterpret_cast<const char *>(data.data()), qsizetype(data.size())), 16,
                        QFont::PreferNoHinting);
    return font.isValid();
}

FontFaceHandleFreetype FreeType::LoadFace(Span<const byte> data, const String &source, int faceIndex, int) {
    if (faceIndex != 0)
        Log::Message(Log::LT_WARNING, "Only the first face of %s is read (Qt's fonts).", source.c_str());
    Face *face = new Face;
    face->data = QByteArray(reinterpret_cast<const char *>(data.data()), qsizetype(data.size()));
    face->font.loadFromData(face->data, 16, QFont::PreferNoHinting);
    face->size = 16;
    if (!face->font.isValid()) {
        Log::Message(Log::LT_ERROR, "Qt cannot read the font face in %s.", source.c_str());
        delete face;
        return 0;
    }
    return reinterpret_cast<FontFaceHandleFreetype>(face);
}

bool FreeType::ReleaseFace(FontFaceHandleFreetype handle) {
    delete faceOf(handle);
    return true;
}

void FreeType::GetFaceStyle(FontFaceHandleFreetype handle, String *family, Style::FontStyle *style,
                            Style::FontWeight *weight) {
    const QRawFont &font = faceOf(handle)->font;
    if (family)
        *family = font.familyName().toStdString();
    if (style)
        *style = font.style() == QFont::StyleNormal ? Style::FontStyle::Normal : Style::FontStyle::Italic;
    if (weight)
        *weight = font.weight() > 0 ? Style::FontWeight(font.weight()) : Style::FontWeight::Normal;
}

bool FreeType::InitialiseFaceHandle(FontFaceHandleFreetype handle, int size, FontGlyphMap &glyphs, FontMetrics &metrics,
                                    bool loadDefaultGlyphs) {
    Face *face = faceOf(handle);
    metrics.size = size;
    if (!setSize(face, size))
        return false;
    const QRawFont &font = face->font;
    if (loadDefaultGlyphs) {
        glyphs.reserve(128);
        for (char32_t code = 32; code <= 126; ++code)
            buildGlyph(font, Character(code), glyphs);
    }
    addReplacement(size, glyphs);

    metrics.ascent = float(font.ascent());
    metrics.descent = float(font.descent());
    metrics.line_spacing = float(font.ascent() + font.descent() + font.leading());
    metrics.underline_position = float(font.underlinePosition());
    metrics.underline_thickness = std::max(float(font.lineThickness()), 1.0f);
    metrics.x_height = font.xHeight() > 0 ? float(font.xHeight()) : 0.5f * metrics.line_spacing;
    metrics.has_ellipsis = font.supportsCharacter(uint(0x2026));
    return true;
}

bool FreeType::AppendGlyph(FontFaceHandleFreetype handle, int size, Character character, FontGlyphMap &glyphs) {
    Face *face = faceOf(handle);
    return setSize(face, size) && buildGlyph(face->font, character, glyphs);
}

int FreeType::GetKerning(FontFaceHandleFreetype handle, int size, Character lhs, Character rhs) {
    Face *face = faceOf(handle);
    // Size 0: the size last set.
    if (!setSize(face, size))
        return 0;
    const QList<quint32> pair{glyphIndex(face->font, lhs), glyphIndex(face->font, rhs)};
    if (pair[0] == 0 || pair[1] == 0)
        return 0;
    const QList<QPointF> plain = face->font.advancesForGlyphIndexes(pair, QRawFont::SeparateAdvances);
    const QList<QPointF> kerned = face->font.advancesForGlyphIndexes(pair, QRawFont::KernedAdvances);
    if (plain.size() != 2 || kerned.size() != 2)
        return 0;
    return qRound(kerned[0].x() - plain[0].x());
}

bool FreeType::HasKerning(FontFaceHandleFreetype handle) {
    return !faceOf(handle)->font.fontTable("kern").isEmpty();
}

}
