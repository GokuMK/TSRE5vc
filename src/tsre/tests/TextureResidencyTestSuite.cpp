#include "TextureResidencyTestSuite.h"

#include <QDebug>
#include <QImage>
#include <QScopedValueRollback>
#include <QTemporaryDir>
#include <memory>
#include <vector>
#include <tsre/renderer/MemoryLog.h>
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

    // The memory log's line (log.txt): the textures' total at the last pass
    // against the budget, and the process's memory.
    TexLib::setResidencyBudget(qint64(1) << 30);
    TexLib::enforceResidency(now + 600000, minAge);
    check(TexLib::residentTotal() > 0, "a residency pass leaves the textures' total for the memory log");
    const QString memory = MemoryLog::line();
    check(memory.contains(" of 1024 MB budget") && memory.contains("not created: none"),
          "the memory log's line: the textures against the budget, no resource failed");
#if defined(_WIN32) || defined(Q_OS_LINUX)
    check(MemoryLog::process().workingSetMB > 0 && !memory.contains("? MB working set"),
          "the memory log's line: the process's memory");
#endif

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

    // The budget following the GPU's room (renderer task 42), reported here
    // by the test: six old textures under a budget of ten, a GPU allowing
    // 8 GB (a margin of 512 MB).
    TexLib::reset();
    std::vector<int> six;
    for (int i = 0; i < 6; ++i) {
        six.push_back(TexLib::addTex(image(QString("Room%1.png").arg(i), QColor(40 * i, 0, 0))));
        texture(six.back())->requested = 1000 + i;
    }
    auto held = [&] {
        int count = 0;
        for (int id : six)
            count += texture(id)->released ? 0 : 1;
        return count;
    };
    constexpr qint64 MB = qint64(1) << 20;
    const qint64 allowed = 8 * GB, margin = 512 * MB;
    TexLib::GpuRoom reported;
    TexLib::setGpuRoomSource([&] { return reported; });
    TexLib::setResidencyBudget(each * 10);
    qint64 at = now;
    TexLib::enforceResidency(at, minAge);
    check(TexLib::currentBudget() == each * 10 && held() == 6, "the GPU's room not known: the budget stays");
    reported = {margin + each * 20, allowed};
    TexLib::enforceResidency(at += 1000, minAge);
    check(TexLib::currentBudget() == each * 10 && held() == 6, "room beyond the margin: the budget stays");
    // Two textures' worth into the margin: the budget is what they hold less
    // that, the pass releasing down to 90% of it.
    reported = {margin - each * 2, allowed};
    TexLib::enforceResidency(at += 1000, minAge);
    check(TexLib::currentBudget() == each * 4 && held() == 3,
          "into the margin: the budget is what the textures hold less what is missing");
    check(MemoryLog::line().contains(" (lowered from "), "the memory log's line: the budget lowered");
    // The driver telling the released memory late: the same room again
    // within 3 s lowers nothing more; told, the budget stays.
    TexLib::enforceResidency(at += 1000, minAge);
    check(TexLib::currentBudget() == each * 4 && held() == 3, "the same room within 3 s of lowering: no lower");
    reported = {margin + each, allowed};
    TexLib::enforceResidency(at += 3000, minAge);
    check(TexLib::currentBudget() == each * 4 && held() == 3, "the released memory told: the budget the same");
    reported = {margin + each * 5, allowed};
    TexLib::enforceResidency(at += 1000, minAge);
    check(TexLib::currentBudget() == each * 8, "more room: the budget goes up");
    reported = {margin + each * 9, allowed};
    TexLib::enforceResidency(at += 1000, minAge);
    check(TexLib::currentBudget() == each * 10, "room enough: the setting's budget again");
    // TSRE using more than allowed: an eighth of the budget at least.
    reported = {-4 * GB, allowed};
    TexLib::enforceResidency(at += 1000, minAge);
    check(TexLib::currentBudget() == each * 10 / 8 && held() == 1, "over the allowed: an eighth of the budget");

    // A GPU resource not created: three quarters of what the textures hold,
    // for a minute.
    reported = {margin + each * 40, allowed};
    for (int id : six)
        TexLib::request(id);
    TexLib::memoryShort();
    TexLib::enforceResidency(at += 3000, minAge);
    check(TexLib::currentBudget() == each * 6 * 3 / 4 && held() == 4,
          "a create failed: three quarters of what the textures hold");
    TexLib::enforceResidency(at += 30000, minAge);
    check(TexLib::currentBudget() == each * 6 * 3 / 4, "and still half a minute later");
    TexLib::enforceResidency(at += 30000, minAge);
    check(TexLib::currentBudget() == each * 10, "a minute later the setting's budget again");
    TexLib::setGpuRoomSource(nullptr);
    TexLib::setResidencyBudget(-1);
    TexLib::reset();

    qInfo() << "[tests:texture-residency] cases=" << (passed + failed) << "passed=" << passed
            << "failed=" << failed;
    return failed == 0 ? 0 : 1;
}
