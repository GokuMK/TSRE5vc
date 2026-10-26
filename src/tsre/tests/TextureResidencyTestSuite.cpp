#include "TextureResidencyTestSuite.h"

#include <QDebug>
#include <QImage>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <memory>
#include <tsre/texture/ImageLib.h>
#include <tsre/texture/TexLib.h>
#include <tsre/texture/Texture.h>

// Texture memory residency (docs/tasks/textures/04-texture-memory.md) on the
// CPU side: textures loaded and waiting for their first draw, released over
// the budget least recently used first, loaded again when asked for. The GPU
// side (evicted handles coming back) needs a QRhi; renderer captures cover it.
int TsreTests::runTextureResidencySuite(bool verbose) {
    int passed = 0, failed = 0;
    auto check = [&](bool ok, const char *name) {
        if (ok) {
            ++passed;
            if (verbose) qInfo() << "[tests:texture-residency] PASS" << name;
        } else {
            ++failed;
            qWarning() << "[tests:texture-residency] FAIL" << name;
        }
    };
    QScopedValueRollback<bool> syncImages(ImageLib::IsThread, false);
    TexLib::reset();
    QTemporaryDir dir;
    // A 64x64 image of one colour per texture, to tell them apart.
    auto image = [&](const QString &name, QColor colour) {
        QImage pixels(64, 64, QImage::Format_RGB32);
        pixels.fill(colour);
        const QString path = dir.filePath(name);
        pixels.save(path);
        return path;
    };
    auto texture = [](int id) { return TexLib::mtex.at(id); };
    auto red = [&](int id) {
        const Texture *t = texture(id);
        return t->loaded && t->imageData != nullptr && t->imageData[0] > 200 && t->imageData[1] < 50;
    };

    // The two edited below asked for as editable; the others display only.
    const int oldest = TexLib::addTex(image("Oldest.png", Qt::red), false, TexLib::Editable);
    const int middle = TexLib::addTex(image("Middle.png", Qt::green));
    const int recent = TexLib::addTex(image("Recent.png", Qt::blue));
    const int edited = TexLib::addTex(image("Edited.png", Qt::red), false, TexLib::Editable);
    const int missing = TexLib::addTex(dir.filePath("Missing.png"));
    const qint64 each = texture(oldest)->estimatedCpuBytes();
    check(each > 0 && texture(oldest)->reloadable && texture(middle)->loaded && texture(recent)->loaded,
          "file textures load reloadable, waiting for their first draw");
    texture(edited)->setEditable();
    check(!texture(edited)->reloadable, "an edited texture is not reloadable");
    check(!texture(missing)->loaded && (texture(missing)->missing || texture(missing)->error),
          "a missing file fails");
    texture(recent)->setEditable();
    check(texture(recent)->displayOnly && texture(recent)->reloadable, "a display-only texture is not edited");
    std::unique_ptr<unsigned char[]> read(texture(recent)->getImageData(8, 8));
    check(read && read[0] < 50 && read[2] > 200 && texture(recent)->reloadable,
          "a display-only texture's pixels are read from a copy");

    // Clock times: the edited one oldest of all, then oldest, middle, and
    // recent within the minimum age.
    texture(edited)->requested = 0;
    texture(oldest)->requested = 1000;
    texture(middle)->requested = 2000;
    texture(recent)->requested = 95000;
    const qint64 now = 100000, minAge = 30000;

    TexLib::setResidencyBudget(0);
    check(TexLib::enforceResidency(now, minAge) == 0, "a budget of 0 releases nothing");
    TexLib::setResidencyBudget(each * 10);
    check(TexLib::enforceResidency(now, minAge) == 0 && !texture(oldest)->released,
          "under the budget nothing is released");

    // Four loaded (the edited one counts, never released): 4 x each over a
    // budget of 3.4 x each; down to 90% of it releases the oldest only.
    TexLib::setResidencyBudget(each * 34 / 10);
    const int released = TexLib::enforceResidency(now, minAge);
    check(released == 1 && texture(oldest)->released && !texture(oldest)->loaded
                  && texture(oldest)->imageData == nullptr,
          "over the budget the least recently used is released, its pixels freed");
    check(!texture(middle)->released && !texture(recent)->released && !texture(edited)->released
                  && texture(edited)->imageData != nullptr,
          "the others stay: newer, or edited");

    // A tight budget: everything old enough goes, the recent and edited stay.
    TexLib::setResidencyBudget(1);
    TexLib::enforceResidency(now, minAge);
    check(texture(middle)->released && !texture(recent)->released && !texture(edited)->released,
          "textures used within the minimum age are never released");

    check(TexLib::request(oldest) && red(oldest) && !texture(oldest)->released,
          "a released texture asked for loads again");
    check(TexLib::addTex(dir.filePath("Middle.png")) == middle && texture(middle)->loaded,
          "asked for again by its path, a released texture loads again under its id");

    TexLib::setResidencyBudget(1);
    TexLib::enforceResidency(now + 200000, minAge);
    check(texture(oldest)->released, "released again");
    texture(oldest)->setEditable();
    check(red(oldest) && texture(oldest)->editable && !texture(oldest)->reloadable,
          "editing a released texture loads its pixels first");

    TexLib::setResidencyBudget(1);
    texture(middle)->requested = 0;
    TexLib::enforceResidency(now + 400000, minAge);
    const int copy = TexLib::cloneTex(middle);
    const Texture *copied = copy >= 0 ? texture(copy) : nullptr;
    check(copied != nullptr && copied->imageData != nullptr && copied->imageData[1] > 100,
          "cloning a released texture copies its pixels");

    const int later = TexLib::addTex(image("Later.png", Qt::green));
    check(TexLib::addTex(dir.filePath("Later.png"), false, TexLib::Editable) == later
                  && !texture(later)->displayOnly && texture(later)->imageData != nullptr,
          "asked for as editable, a display-only texture becomes editable");
    texture(later)->setEditable();
    check(!texture(later)->reloadable && texture(later)->editable, "and is edited then");

    TexLib::setResidencyBudget(-1);
    check(TexLib::residencyBudget() > 0, "without a test budget, the setting's or the automatic one");
    // The automatic budget (the setting 0): a discrete GPU's memory, else RAM.
    constexpr qint64 GB = qint64(1) << 30;
    TexLib::setGpuMemory(10 * GB, true);
    check(TexLib::residencyBudget() == 6 * GB, "with a discrete GPU of 10 GB the budget is 6 GB");
    TexLib::setGpuMemory(64 * GB, true);
    check(TexLib::residencyBudget() == 16 * GB, "at most 16 GB");
    TexLib::setGpuMemory(512 * (qint64(1) << 20), false);
    const qint64 integrated = TexLib::residencyBudget();
    check(integrated >= GB && integrated <= 4 * GB, "an integrated GPU's own memory counts not: RAM, 1 to 4 GB");
    TexLib::setGpuMemory(0, false);
    TexLib::reset();

    qInfo() << "[tests:texture-residency] cases=" << (passed + failed) << "passed=" << passed
            << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
