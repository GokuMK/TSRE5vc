#include <tsre/tests/ParserExponentTestSuite.h>
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ParserX.h>
#include <QDebug>
#include <QString>
#include <cmath>
#include <cstring>
#include <limits>

int TsreTests::runParserExponentSuite() {
    const struct {
        const char *text;
        float expected;
    } cases[] = {
        {"1e+006", 1000000.0f}, // EUROPE1 PickupItem 325
        {"1E+006", 1000000.0f},
        {"1e006", 1000000.0f},
        {"-2E+3", -2000.0f},
        {"1.25e+2", 125.0f},
        {"1.25e-1", 0.125f},
        {"-1.25E-1", -0.125f},
        {"1e-005", 0.00001f},
        {"1e+0", 1.0f},
        {"1e-0", 1.0f},
        {"0e+6", 0.0f},
        {"1e+2+3", 103.0f},
        {"2+1e+2", 102.0f},
        {"1e+2+2e+1", 120.0f},
        {"1e+2cm", 1.0f},
        {"1e+2cm+2", 3.0f},
        {"2e+1ft", 6.096f},
        {"12.5", 12.5f},
        {"2+3", 5.0f},
    };
    int passed = 0, failed = 0;
    for (bool inside : {false, true}) {
        for (const auto &test : cases) {
            const QString text = QString::fromLatin1(test.text) + " 42 )";
            const int bytes = text.size() * sizeof(char16_t);
            auto *memory = new unsigned char[bytes];
            std::memcpy(memory, text.utf16(), bytes);
            FileBuffer buffer(memory, bytes);
            bool ok = true;
            const float value = inside ? ParserX::GetNumberInside(&buffer, &ok)
                                       : ParserX::GetNumber(&buffer);
            const float tolerance = 4 * std::numeric_limits<float>::epsilon()
                    * std::fabs(test.expected);
            const bool valueOk = ok && std::fabs(value - test.expected) <= tolerance;
            const float next = inside ? ParserX::GetNumberInside(&buffer, &ok)
                                      : ParserX::GetNumber(&buffer);
            const bool nextOk = ok && next == 42.0f;
            ParserX::GetNumberInside(&buffer, &ok);
            const bool endOk = !ok && buffer.getShort() == ')';
            if (valueOk && nextOk && endOk) {
                ++passed;
            } else {
                ++failed;
                qWarning() << "[tests:parser-exponents] FAIL" << inside << test.text
                           << "value" << value << "expected" << test.expected
                           << "next" << next << "closing parenthesis preserved" << endOk;
            }
        }
    }
    qInfo() << "[tests:parser-exponents] passed=" << passed << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
