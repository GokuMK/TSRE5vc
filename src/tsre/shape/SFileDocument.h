#pragma once
// Private source model: block records reference contiguous native numeric
// arrays. Node is a non-owning handle, valid while its Document exists
// (including across arena growth, but not document reload/replacement). Exceptional
// lexemes/labels/opaque data live in sparse side tables.
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <memory>
#include <tsre/fileFunctions/TS.h>
#include <vector>
namespace SFileDetail {
struct Storage;
struct Children;
// Compact-only runtime tables. Complete always retains editable source blocks.
// Points/normals/UVs contain float bits; vertices contain point/normal/UV indices.
struct PackedTable {
    int width = 0;
    std::vector<quint32> words;
    size_t rows() const { return width ? words.size() / width : 0; }
    float number(size_t index) const;
    int integer(size_t index) const;
};
struct Node {
    Storage *store = nullptr;
    quint32 index = 0;
    explicit operator bool() const { return store != nullptr; }
    const Node *operator->() const { return this; }
    Node *operator->() { return this; }
    const Node &operator*() const { return *this; }
    Node &operator*() { return *this; }
    QString name() const;
    QString label() const;
    QByteArray tail() const;
    TS::TokenId id() const;
    const PackedTable *packed() const;
    Node child(const QString &name) const;
    Node child(TS::TokenId id) const;
    Children children(const QString &name = {}) const;
    int scalarCount() const;
    char scalarType(int index) const;
    QString scalar(int index, const QString &fallback = {}) const;
    double number(int index, double fallback = 0) const;
    int integer(int index, int fallback = 0) const;
    bool setScalar(int index, const QString &value, QString *error = nullptr);
    void appendCopy(Node source); // Explicit subtree copy, primarily for fixtures/editing.
};
struct Children {
    Node parent;
    TS::TokenId filter = 0;
    QString unknownName;
    bool filtered = false;
    struct Iterator {
        const Children *range;
        quint32 current;
        Node operator*() const { return {range->parent.store, current}; }
        Iterator &operator++();
        bool operator!=(const Iterator &other) const { return current != other.current; }
        void seek();
    };
    Iterator begin() const;
    Iterator end() const;
    bool empty() const;
    size_t size() const;
    Node operator[](size_t index) const;
    Node front() const { return *begin(); }
};
struct Document {
    std::unique_ptr<Storage> storage;
    Node root;
    std::vector<Node> extraRoots;
    QStringList diagnostics;
    char fileKind = 's';
    bool binary = false, compressed = false, damaged = false, partial = false, compact = false;
    quint64 skippedBlocks = 0;
    QByteArray original;
    Document();
    ~Document();
    Document(const Document &);
    Document &operator=(const Document &);
    Document(Document &&) noexcept;
    Document &operator=(Document &&) noexcept;
    bool read(const QString &path, bool firstLod = false, bool compact = false);
    bool readBytes(QByteArray bytes, bool firstLod = false, bool compact = false);
    // Rendering may omit this section; damaged/original still protect full saving.
    bool damageConfinedToAnimations() const;
    QByteArray encode(bool binary, bool compressed, QString &error) const;
    bool save(const QString &path, bool binary, bool compressed, QString &error) const;
    qsizetype storageBytes() const;
    size_t blockCount() const;
    size_t scalarCount() const;
    static size_t recordBytes();
};
} // namespace SFileDetail
