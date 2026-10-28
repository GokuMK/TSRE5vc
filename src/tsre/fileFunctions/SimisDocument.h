#pragma once
#include <QHash>
#include <QString>
#include <QStringList>
#include <QAnyStringView>
#include <QStringView>
#include <QVector>
#include <memory>
#include <tsre/fileFunctions/SimisTree.h>
#include <tsre/units/Quantity.h>

// A SIMIS text file with its includes (task driving 01), read as Open Rails
// reads them. An include's path is relative to the folder of the file
// holding it, `[[SameName]]` standing for that file's name; TSRE also
// takes a root at its start (the user's, 2026-10-28): [[Game]], [[Trains]],
// [[Trainset]], [[Global]], [[Sound]], [[Vehicle]] (the vehicle's own
// folder) and [[Here]]. Every path must stay inside the game folder.
//
// Each file is read once into a SimisTree shared by every document
// including it (a cache by path and modification time, while anything
// holds the tree); each document resolves its own includes, since
// [[Vehicle]] differs between vehicles. An include that does not balance
// its parentheses is read in place of its include item instead, as Open
// Rails reads every include, so the including file gets a tree of its own.
// Made once and never changed: shared by const pointer between threads.
class SimisDocument {
  public:
    struct Roots {
        // The game folder (MSTS's): [[Game]] and the folders in it; empty
        // for none (no root but [[Here]], [[Vehicle]] and no folder check).
        QString game;
        // The vehicle's folder for [[Vehicle]]; empty where not known.
        QString vehicle;
    };
    struct Problem {
        QString file;
        // 0 for the file as a whole.
        int line = 0;
        QString message;
    };
    // An item of the document: a value or a block (items() follow
    // includes, so never an include); none when its tree is null.
    struct Item {
        const SimisTree *tree = nullptr;
        int node = -1;
        bool operator==(const Item &o) const { return tree == o.tree && node == o.node; }
    };

    // The file at `path` with its includes; problems() tell what was wrong,
    // ok() whether the file itself was read.
    static std::shared_ptr<const SimisDocument> read(const QString &path, const Roots &roots);
    // A path written in a file to the path it means: `written` with '\'
    // as '/', `[[SameName]]` as the file name of `holder` (the file it was
    // written in), a root, else relative to the folder of `holder`;
    // cleaned. Empty, with `problem`, for an unknown root, an absolute path
    // or one leaving the game folder.
    static QString resolve(QStringView written, const QString &holder, const Roots &roots,
                           QString *problem = nullptr);

    bool ok() const { return tree != nullptr; }
    // The items at the top of the file, or in a block (none for a value),
    // followed into includes: in file order, an include's items where it
    // is written.
    QVector<Item> items() const;
    QVector<Item> items(Item block) const;
    // A block's values (items that are neither blocks nor includes).
    QVector<Item> values(Item block) const;
    // The blocks at a path of names, as Open Rails dispatches them:
    // "wagon(coupling(spring" (any case), in file order; last() the last
    // (a key given again replaces the earlier), invalid when none.
    QVector<Item> all(QAnyStringView path) const;
    Item last(QAnyStringView path) const;

    bool isBlock(Item item) const;
    QStringView text(Item item) const;
    bool is(Item item, QAnyStringView name) const;
    // A value as a number with its unit, in SI (Units::fromFileToken: the
    // quantity's units, `defaultUnit` without one written).
    bool number(Item value, Units::Quantity quantity, double &si, QStringView defaultUnit = {}) const;
    // An item as SIMIS text: a value (quoted where it was or must be), a
    // block with its items, includes followed; for readers taking text
    // (EngLightsBlock).
    QString write(Item item) const;
    // Something in a block is included through [[Vehicle]], the one root
    // that differs between vehicles: two vehicles sharing the block's file
    // can read different items in it.
    bool readsVehicleRoot(Item block) const;
    // Where an item was written.
    QString file(Item item) const;
    int line(Item item) const;

    // The file read, then every file its includes read, in reading order.
    const QStringList &files() const { return paths; }
    const QVector<Problem> &problems() const { return found; }
    // The trees it holds (shared ones too), for measuring memory.
    QVector<const SimisTree *> trees() const;

  private:
    std::shared_ptr<const SimisTree> tree;
    // An include (its tree and node) to the tree of the file it reads.
    QHash<QPair<const SimisTree *, int>, const SimisTree *> links;
    QVector<std::shared_ptr<const SimisTree>> kept;
    QStringList paths;
    QVector<Problem> found;
    friend struct SimisDocumentReader;
    void collect(const SimisTree *from, int first, QVector<Item> &out, int depth) const;
    void write(const SimisTree *tree, int index, QString &out, int depth) const;
    bool readsVehicleRoot(const SimisTree *tree, int first, int depth) const;
};
