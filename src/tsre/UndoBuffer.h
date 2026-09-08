#pragma once
#include <QByteArray>
#include <memory>

// Main-thread owned. Workers see only immutable copies, never this object.
class UndoBuffer : public std::enable_shared_from_this<UndoBuffer> {
public:
    explicit UndoBuffer(const QByteArray &bytes);
    QByteArray bytes() const;
    qsizetype rawSize() const { return raw.size(); }
    qsizetype compressedSize() const { return compressed.size(); }
    void compressLater();
    // Called by the editor's existing undo timer, even with no open action.
    static void pump();
    static bool busy();
private:
    QByteArray raw, compressed;
    qsizetype size;
    bool queued = false;
};

struct UndoSnapshot {
    virtual ~UndoSnapshot() = default;
    std::shared_ptr<UndoBuffer> buffer;
    virtual bool restore() = 0;
    virtual bool targetValid() const = 0;
};
