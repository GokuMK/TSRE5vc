#pragma once
#include <QStringView>
#include <charconv>
#include <tsre/units/Quantity.h>

// Task core si-units: a number as MSTS's text files write it, with the unit
// written after it, read the same way by every reader. The grammar is a
// template over a source of characters, so ParserX reads it straight from
// its FileBuffer and other readers from a text (Units::readFileNumber,
// Units::fromFileToken in UnitConversion.h), in one pass each.
namespace Units {

// A number as MSTS's text files write it, at the start of a text, with the
// unit written after it ("80t", "-1mph", "36in/2", "2.5*(ft^3)", a '+' sum
// "2t+500kg"): the one reading of numbers with units that every reader
// shares (ParserX gathers the characters at its place, readers on
// SimisTextReader give their tokens to fromFileToken). The digits are
// read exactly (correctly rounded); a comma followed by a digit is the
// point, as MSTS reads it.
struct FileNumber {
    // Digits were there (a lone "-" or "." reads as 0 and takes itself).
    bool found = false;
    // A unit was written, and whether the quantity takes it (an unknown
    // unit leaves the number as written).
    bool hasUnit = false;
    bool unitKnown = true;
    // As written, and in SI.
    double number = 0.0;
    double si = 0.0;
    // The characters the number, its unit and a sum took.
    int length = 0;
    // fromFileToken: the token held nothing else (but quotes, a comment
    // after '#', commas and spaces around it).
    bool whole = false;
};

// The unit read after a number, in SI (UnitConversion.cpp).
void convertFileNumber(FileNumber &read, const char16_t *unit, int length, Quantity quantity,
                       QStringView defaultUnit);

// A source gives peek(ahead) (0 past its end) and next(). Reads at its
// place: "-12.5e3", the unit ("80t", "km/h", "36in/2", "2.5*(ft^3)"), a
// '+' sum ("2t+500kg"); leaves it after what it took. `length` is left to
// the caller (the source knows where it is).
template <class Source>
FileNumber readFileNumberFrom(Source &source, Quantity quantity, QStringView defaultUnit = {}) {
    FileNumber out;
    const auto digit = [](char16_t c) { return c >= '0' && c <= '9'; };
    // The digits, kept for std::from_chars. Up to 19 significant digits also
    // make an integer with a decimal exponent: scaled by an exact power of
    // ten when that is exact too (Clinger's fast path), correctly rounded as
    // std::from_chars would give it.
    char digits[96];
    int n = 0;
    unsigned long long mantissa = 0;
    int significant = 0, scale = 0;
    bool fits = true;
    const auto put = [&](char c) {
        if (n < int(sizeof(digits)))
            digits[n++] = c;
    };
    const auto keep = [&](char16_t c, bool fraction) {
        put(char(c));
        out.found = true;
        if (fraction)
            --scale;
        if (mantissa == 0 && c == '0')
            return;
        if (significant < 19) {
            mantissa = mantissa * 10 + (c - '0');
            ++significant;
        } else {
            fits = false;
        }
    };
    bool negative = false;
    if (source.peek() == '-') {
        negative = true;
        put('-');
        source.next();
    }
    while (digit(source.peek())) {
        keep(source.peek(), false);
        source.next();
    }
    // A comma for the point when a digit follows, as MSTS reads it.
    if (source.peek() == '.' || (source.peek() == ',' && digit(source.peek(1)))) {
        put('.');
        source.next();
        while (digit(source.peek())) {
            keep(source.peek(), true);
            source.next();
        }
    }
    int exponent = 0;
    if (source.peek() == 'e' || source.peek() == 'E') {
        bool below = false;
        put('e');
        source.next();
        if (source.peek() == '-') {
            below = true;
            put('-');
            source.next();
        } else if (source.peek() == '+') {
            source.next();
        }
        while (digit(source.peek())) {
            put(char(source.peek()));
            if (exponent < 10000)
                exponent = exponent * 10 + (source.peek() - '0');
            source.next();
        }
        if (below)
            exponent = -exponent;
    }
    static constexpr double Powers[23] = {1e0, 1e1, 1e2, 1e3, 1e4, 1e5, 1e6, 1e7, 1e8, 1e9, 1e10, 1e11,
                                          1e12, 1e13, 1e14, 1e15, 1e16, 1e17, 1e18, 1e19, 1e20, 1e21, 1e22};
    const int power = scale + exponent;
    double value = 0.0;
    if (fits && mantissa <= (1ULL << 53) && power >= -22 && power <= 22) {
        value = power >= 0 ? double(mantissa) * Powers[power] : double(mantissa) / Powers[-power];
        if (negative)
            value = -value;
    } else {
        // As far as the digits make a number ("1e", "-" alone: what is before).
        std::from_chars(digits, digits + n, value);
    }
    // The unit: letters (or "*(" for a volume), then letters, digits, '/',
    // '^', '-', '*' and brackets that open in it: not the block's closing
    // bracket.
    char16_t unit[33];
    int length = 0, depth = 0;
    for (;;) {
        const char16_t c = source.peek();
        const bool letter = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
        const bool inUnit = length > 0
                && (digit(c) || c == '/' || c == '^' || c == '-' || c == '*' || c == '(' || (c == ')' && depth > 0));
        if (!letter && !inUnit && !(length == 0 && c == '*'))
            break;
        if (c == '(')
            ++depth;
        else if (c == ')')
            --depth;
        if (length < 33)
            unit[length] = c;
        ++length;
        source.next();
    }
    out.hasUnit = length > 0;
    out.number = value;
    out.si = value;
    if (out.hasUnit || !defaultUnit.isEmpty())
        convertFileNumber(out, unit, length, quantity, defaultUnit);
    // A sum: "2t+500kg".
    const char16_t after = source.peek(1);
    if (source.peek() == '+' && (digit(after) || after == '.' || after == '-')) {
        source.next();
        const FileNumber rest = readFileNumberFrom(source, quantity, defaultUnit);
        out.number += rest.number;
        out.si += rest.si;
        out.hasUnit = out.hasUnit || rest.hasUnit;
        out.unitKnown = out.unitKnown && rest.unitKnown;
    }
    return out;
}

}
