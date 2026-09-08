# FileBuffer: binary SIMIS reading, recovery and block writing

Updated: 2026-09-09. Describes the API in token-ID commit `8c85bb1` on
`feature/native-token-ids`, not a new parser implementation.

**Recovery policy matters:** TSRE should retain usable content for viewing.
The current helpers provide checked reads, but do not implement recovery.
Some migrated consumers currently abort an entire shape/terrain load or roll
back a world-file load on `ParseError`. That behavior is under review and is
not the recommended TSRE policy. The recovery example below is an integration
pattern using the existing API; it has **not** been installed in those consumers.

This guide covers the buffer, the small `SimisReader.h` helpers, old/new usage
and an example writer. It is not a general S/W/T converter or an ACE reader.
For token assignments and the deliberate prototype/network ID changes, see
[native token IDs](native-token-ids.md).

## 1. Responsibilities and source files

| Component | Responsibility |
| --- | --- |
| [FileBuffer.h](../../src/tsre/fileFunctions/FileBuffer.h), [FileBuffer.cpp](../../src/tsre/fileFunctions/FileBuffer.cpp) | Own bytes, maintain a cursor, read scalar values, validate binary block framing and impose temporary upper bounds |
| [ReadFile.cpp](../../src/tsre/fileFunctions/ReadFile.cpp) | Load a file; `read()` handles the existing SIMIS compression wrappers; `readRAW()` copies bytes without decompression |
| [SimisReader.h](../../src/tsre/fileFunctions/SimisReader.h) | Required-token block scope and a minimum-size array-count check |
| [TS.h](../../src/tsre/fileFunctions/TS.h), [TS.cpp](../../src/tsre/fileFunctions/TS.cpp) | Complete token IDs and symbolic-name lookup |
| Consumer: TFile, Tile, SFile, etc. | File schema, required versus optional fields, payload interpretation, recovery and rendering suitability |
| `QDataStream` / existing format writer | Serialize fields and block headers; FileBuffer has **no write-block or save API** |

SIMIS block framing does not make every payload a child-block list. A payload
can mix counts, scalar values, strings and nested blocks. Only the schema tells
you when a token header is expected. Never search raw heights, triangle indices
or arbitrary bytes for token-looking numbers.

### 1.1. Current consumers

Inventory checked on 2026-09-09. This distinguishes the shared block parser
from classes that merely use FileBuffer as a byte/text buffer.

| Classes | Format / role |
| --- | --- |
| [TFile](../../src/tsre/world/TFile.cpp) | Binary terrain `.t` root, samples, shaders, patch sets and extension fields |
| [Tile](../../src/tsre/world/Tile.cpp), `Tile::ViewDbSphere` | Binary `.w`/`.ws` roots, child/object framing, view spheres and inline watermark handling |
| [WorldObj](../../src/tsre/world/objects/WorldObj.cpp) and subclasses below | Object factories and binary property setters invoked inside Tile's block scopes |
| [SFile](../../src/tsre/shape/SFile.cpp), `SFile::Animation`, [SFileC](../../src/tsre/shape/SFileC.cpp) | Binary `.s` sections, geometry/LOD data, animation nodes/controllers/keys |
| [RouteEditorClient](../../src/routeEditor/RouteEditorClient.cpp), [RouteEditorServer](../../src/routeEditor/RouteEditorServer.cpp) | NetworkToken reads/writes full message IDs; its outer envelope is not a SIMIS child-block sequence |
| [Terrain](../../src/tsre/world/Terrain.cpp), [TerrainClient](../../src/tsre/world/TerrainClient.cpp) | Indirect consumers: pass received terrain descriptors to TFile |

The binary world-property readers are `WorldObj`, `StaticObj`, `TrackObj`,
`DynTrackObj`, `ForestObj`, `TransferObj`, `PlatformObj`, `CarSpawnerObj`,
`LevelCrObj`, `PickupObj`, `HazardObj`, `SignalObj`, `SpeedpostObj`,
`SoundSourceObj` and `SoundRegionObj`. Gantry/collision objects reuse StaticObj;
sidings reuse PlatformObj. DynTrackObj and SignalObj also use Simis::Block for
nested structures. Other property setters read positional fields under scopes
provided by Tile. This inventory is not a claim of complete binary field support.

**Legacy paths outside the new block parser:**

- [QuadTree / QuadTree::QuadTile](../../src/tsre/world/QuadTree.cpp): `.td`/`.tdl`
  input still skips fixed header bytes and reads packed tree data using
  FileBuffer. The minimum 2026-09-09 correction only replaces the writer's
  `0x84`/`0x87` literals with `TS::terrain_desc` (132) and
  `TS::terrain_desc_tiles` (135). Header skipping, tree data and read behavior
  are unchanged; this is not an unfinished reader conversion to carry out now.
- `ParserX`, `SFileX`, TDB/TSectionDAT text paths and similar consumers use
  FileBuffer without the binary block helpers.
- Terrain RAW readers use positional bytes, not child-token traversal.
- New `AceLib`/`AceDocument` decoding is separate from these helpers;
  `AceLibLegacy` uses FileBuffer but does not use the new block traversal.

### Scope and completeness

The complete-ID mechanism and basic framing helpers are implemented. The
combined parser integration is **not yet a finished recovery-first TSRE
replacement**. Required behavior corrections, legacy API issues and optional
format expansion are separated in the [TODO list](#111-status-and-follow-up-todos).

## 2. What changed from the old FileBuffer

| Before | Current behavior |
| --- | --- |
| `getToken()` returned a signed integer after subtracting `tokenOffset` | Returns the complete unsigned `TS::TokenId` DWORD; offset member/setter removed |
| Pointer casts for scalar reads | Little-endian, unaligned-safe integer decoding; signed values/floats preserve their bit representation through `memcpy` |
| Manual header skips such as `off += 9`, assuming an empty label | `readBlock()`, `readBlockEnd()`, `skipLabel()` and explicit body/payload/end offsets |
| Reads had no parent boundary | `ScopedLimit` can bound positional reads to their containing block |
| `findToken()` silently reached EOF on absence | Throws `ParseError` on absence or malformed framing; now searches only up to `readEnd()` |
| Heap-allocated `getString(start, end)` with legacy filtering | Additional by-value `readString()` for a length-prefixed UTF-16LE payload string; the old method still exists |
| Consumers guessed binary format from the first root-token value | `isBinarySimis()` checks the usual decompressed binary subheader |
| BOM/header probes could index very short inputs | Minimum-size checks were added to BOM helpers and `ReadFile::read()` probes |

The changes to scalar getters affect shared FileBuffer users, not only token
reads. They are not evidence that every old text or raw-data parser has been
converted to checked parsing.

## 3. Ownership, file loading and offsets

`FileBuffer(unsigned char*, int)` **takes ownership** of the pointer and its
destructor calls `delete[]`. Give it a compatible `new unsigned char[]`
allocation, not a QByteArray's storage, a stack array or borrowed memory.

`ReadFile::read()` and `readRAW()` return a newly allocated FileBuffer.
Prefer `std::unique_ptr<FileBuffer>` to manual deletion. The QFile itself remains
owned by the caller.

Do not assume the buffer is NUL-terminated. `readRAW()` allocates an extra zero
byte outside `length`, but that is not a guarantee of the general constructor
or `read()`. Binary operations must use explicit byte lengths.

The examples use these includes:

```cpp
#include <tsre/fileFunctions/FileBuffer.h>
#include <tsre/fileFunctions/ReadFile.h>
#include <tsre/fileFunctions/SimisReader.h>
#include <QByteArray>
#include <QDataStream>
#include <QFile>
#include <QSaveFile>
#include <QStringList>
#include <array>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
```

A deliberately binary-only file-opening helper:

```cpp
std::unique_ptr<FileBuffer> openBinaryExample(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error(file.errorString().toStdString());

    std::unique_ptr<FileBuffer> data(ReadFile::read(&file));
    if (!data->isBinarySimis())
        throw std::runtime_error("This example handles binary SIMIS only");

    data->off = 32; // Usual decompressed binary file: next field is root ID.
    return data;   // Buffer owns its bytes; QFile can now close.
}
```

The `isBinarySimis()` failure above limits this example; it is **not** a reason
to remove TSRE's Unicode/text fallback. A dual-format consumer should dispatch
nonbinary files to its existing text reader.

Important offset rules:

- `off`, `length` and block ends are absolute **byte** offsets stored as `int`.
  Every end is exclusive.
- A usual decompressed binary file has its root ID at byte 32. FileBuffer does
  not seek there automatically.
- `isBinarySimis()` checks `length >= 32`, `JINX` at byte 16 and `b` at byte
  23. It does not move the cursor, decompress data, validate the outer signature,
  validate the root ID, or identify every SIMIS variant.
- A raw block fragment starts at its own byte zero. An embedded file in a network
  message starts at the consumer's known offset. Do not blindly reset either to
  32; the detector also does not inspect an embedded file relative to `off`.
- The current storage model is not suitable for buffers beyond `INT_MAX` bytes,
  even though a wire length is a uint32.

To construct a buffer from generated/test bytes:

```cpp
std::unique_ptr<FileBuffer> copyBytes(const QByteArray& bytes) {
    if (bytes.size() > std::numeric_limits<int>::max())
        throw std::length_error("FileBuffer uses int offsets");

    auto owned = std::make_unique<unsigned char[]>(bytes.size());
    if (!bytes.isEmpty())
        std::memcpy(owned.get(), bytes.constData(), bytes.size());
    auto result = std::make_unique<FileBuffer>(owned.get(), int(bytes.size()));
    owned.release(); // Ownership transferred to FileBuffer.
    return result;
}
```

Do not copy FileBuffer by ordinary C++ copy construction/assignment: the class
has not disabled the implicitly generated shallow-copy operations, which can
produce shared ownership and double deletion. The distinct
`FileBuffer(const FileBuffer*)` constructor deep-copies the bytes but starts
with `off == 0` and no active limit; it is not a parser-state clone.

## 4. Binary block layout

For a block whose ID begins at byte `s`:

| Bytes | Type / meaning |
| --- | --- |
| `s .. s+3` | uint32 little-endian complete token ID |
| `s+4 .. s+7` | uint32 little-endian body length `L` |
| `s+8` | uint8 label length `C`, in UTF-16 code units |
| next `2*C` bytes | Label, UTF-16LE, no terminator |
| remaining body | Schema-specific payload, possibly including children |

The relationships are:

```text
body    = s + 8
payload = body + 1 + 2*C
end     = body + L
L       = 1 + 2*C + payload byte count
```

The label is part of the body length; the ID and length DWORDs are not.
An empty payload with an empty label still has `L == 1` and occupies 9 bytes.
A zero body length is rejected.

Labels and payload strings are different:

- Block label: **uint8** UTF-16 code-unit count, maximum 255.
- Usual payload string: **uint16** UTF-16 code-unit count, maximum 65,535.
- Neither is a byte count or a Unicode glyph count; surrogate pairs occupy two
  code units. This API does not validate Unicode surrogate pairing.
- No terminator is included. Do not use QDataStream's QString serialization as
  a substitute for the SIMIS string layout.

## 5. API and cursor reference

| Operation | Cursor effect on success | Checks / qualifications |
| --- | --- | --- |
| `getToken()` | +4 bytes | Always checks availability; returns full uint32, including unknown/high-bit IDs |
| `getUint()`, `getInt()`, `getFloat()` | +4 bytes | Bounds-checked **only with an active ScopedLimit** |
| `getShort()`, `getSignedShort()` | +2 bytes | Same scoped-check rule |
| `get()` | +1 byte | Same scoped-check rule |
| `readEnd()` | None | Active limit, otherwise buffer length |
| `require(n)` | None | Always checks nonnegative count/cursor and remaining bytes up to readEnd |
| `readBlock()` | Stops at `body`, the label-count byte | Reads full ID and length, validates body and label fit; returns `{id, body, payload, end}` |
| `readBlockEnd()` | Stops at `body` | Use only after the ID was consumed; validates length and label, returns end |
| `skipLabel()` | Advances across uint8 count and label | Always checked against current readEnd; caller supplies the proper block scope |
| `readString()` | Advances across uint16 count and UTF-16LE data | Always checked against current readEnd; returns QString by value |
| `getString(start, end)` | Does **not** move off | Returns an owning QString pointer; validates range only in a scope; retains legacy filtering |
| `findToken(id)` | On match: points to the **length DWORD** | Forward sibling search; validates/skips other blocks; throws if absent |
| `ScopedLimit(data, end)` | Does not seek | Installs a narrower upper bound; destruction restores previous bound only |
| `Simis::Block(data, id)` | Construction: matching block's payload; destruction: matching block's end | Required sibling search plus limit; destructor also seeks during exception unwinding |
| `Simis::count(data, minimumItemBytes)` | Reads int32 count | Rejects negative count or one impossible to fit using the supplied minimum size |

**A block header does not install its own scope.** After `readBlock()`, use
`ScopedLimit(data, child.end)` before payload getters. Otherwise, for example,
`readString()` can read beyond that child into a sibling while still remaining
inside the file.

ScopedLimit is an **upper-bound guard**, not a separate byte slice. It does not
prevent you from manually rewinding `off` before a child's start. Direct array
access and assignments to public `off` bypass checks. Use `require(n)` before
manual skips/copies, and do not change `data`, `length`, call `toUtf16()` or
`insertFile()` while a binary scope is active.

A failing read is not transactional: some header/count bytes may already have
been consumed. Neither ParseError nor ScopedLimit automatically restores off.

## 6. Old versus new loading

### Token identity

Historical code, which no longer compiles with this API:

```cpp
data->setTokenOffset(261844); // Old world-file normalization.
int id = data->getToken();
if (id == TS::Static) {
    // ...
}
```

Current equivalent identity handling:

```cpp
TS::TokenId id = data->getToken();
if (id == TS::Static) { // Native complete ID 0x00040003.
    // ...
}
```

Do not subtract 261844, add 300, mask to 16 bits or infer the namespace from
the file type. Do not use `TS::error`/zero or `0xFFFFFFFF` as an EOF sentinel;
all uint32 patterns must remain distinguishable as token data.

### One block and its positional payload

An old-style leaf read, shown only for comparison, assumed an empty label and
trusted lengths:

```cpp
int id = data->getToken(); // Under the old ID conventions.
int bodyLength = data->getInt();
int end = data->off + bodyLength;
data->off++;             // Assumes label count is zero.
if (id == TS::UiD)
    uid = data->getUint();
data->off = end;
```

Current checked equivalent for a caller that specifically expects a UiD block:

```cpp
quint32 readUidBlock(FileBuffer& data) {
    // On entry: off points to a UiD block's ID, not its length or payload.
    const auto child = data.readBlock();
    FileBuffer::ScopedLimit scope(data, child.end);
    data.off = child.payload; // Or data.skipLabel(), starting at child.body.
    if (child.id != TS::UiD)
        throw FileBuffer::ParseError("Expected UiD block");

    const quint32 uid = data.getUint(); // Positional value, not a token.
    data.off = child.end;               // Skip any unconsumed trailing bytes.
    return uid;
}
```

This leaf helper throws on failure; its caller decides whether to omit that
field, use a default, skip an object or stop a container. It must not be taken
as a recommendation to abort an entire file.

For a dispatch API where the ID was already read, use:

```cpp
const TS::TokenId id = data.getToken();
const int end = data.readBlockEnd(); // Do not call readBlock(): ID is consumed.
FileBuffer::ScopedLimit scope(data, end);
data.skipLabel();
// Decode the payload appropriate to id.
data.off = end;
```

## 7. Optional fields and recovery-first viewing

For unordered/optional children, walk the siblings once and dispatch on their
IDs. Do **not** call a throwing search once per optional field.

The following example reads a subset of Static object properties. It keeps
successfully decoded values, skips unknown children and can continue after a
malformed property payload when that property's header established a usable
end. It does not instantiate a WorldObj, load shapes or apply TSRE's coordinate
conversions. Values are in their on-disk coordinate convention.

```cpp
struct PreviewFields {
    std::optional<quint32> uid;
    std::optional<std::array<float, 3>> position;
    std::optional<QString> fileName;
    QStringList warnings;
    bool stoppedAtMalformedHeader = false;
};

// On entry: off is at the first property ID inside a validated object block.
// parentEnd is that object's already-validated exclusive end.
PreviewFields readObjectChildren(FileBuffer& data, int parentEnd) {
    PreviewFields result;
    FileBuffer::ScopedLimit parent(data, parentEnd);

    while (data.off < parentEnd) {
        const int headerOffset = data.off;
        FileBuffer::Block child;
        try {
            child = data.readBlock();
        } catch (const FileBuffer::ParseError& error) {
            result.warnings << QStringLiteral("Bad child header at byte %1: %2")
                .arg(headerOffset).arg(QString::fromUtf8(error.what()));
            result.stoppedAtMalformedHeader = true;
            data.off = parentEnd; // No reliable child boundary to resume from.
            break;                // Keep fields already recovered.
        }

        try {
            FileBuffer::ScopedLimit field(data, child.end);
            data.off = child.payload;
            switch (child.id) {
            case TS::UiD: {
                const auto value = data.getUint();
                result.uid = value;
                break;
            }
            case TS::Position: {
                const std::array<float, 3> value{
                    data.getFloat(), data.getFloat(), data.getFloat()
                };
                result.position = value; // Commit only after all three reads.
                break;
            }
            case TS::FileName: {
                const auto value = data.readString();
                result.fileName = value;
                break;
            }
            default:
                break; // Optional/unknown property: skip its opaque payload.
            }
        } catch (const FileBuffer::ParseError& error) {
            result.warnings << QStringLiteral("%1 at byte %2: %3")
                .arg(TS::describe(child.id)).arg(headerOffset)
                .arg(QString::fromUtf8(error.what()));
            // Do not clear successful fields, including an earlier duplicate.
        }
        data.off = child.end; // Valid framing lets us try the next sibling.
    }
    return result;
}
```

This example distinguishes two cases:

1. **Validated child framing, unusable payload:** skip to that child's end and
   continue with the next sibling. A failed duplicate property does not erase
   the earlier successfully decoded value.
2. **Unusable child framing:** readBlock did not provide a reliable next-child
   position. Stop this container and preserve earlier results. If its enclosing
   object boundary is valid, an outer loop can continue with later objects.

This is not arbitrary resynchronization. A plausible-looking length does not
prove that corrupted data contains a correct boundary, and scanning raw bytes
for a known token risks interpreting payload as structure. More ambitious
repair requires format-specific evidence.

For shape viewing, apply the same principle at a useful unit such as a
controller, primitive group or sub-object, while checking dependencies before
rendering recovered geometry. Safely omitting an unusable unit does not require
discarding all independent units. A catch at the whole-file level cannot provide
that behavior by itself.

Preserving content for viewing is also different from silently saving a
partially understood file. Report incomplete recovery to the caller and avoid
automatic overwrite of the source. Unknown payloads skipped by these examples
are **not** retained for lossless saving.

### Required searches are explicit policy choices

This is the current strict convenience API:

```cpp
quint32 readRequiredUid(FileBuffer& data) {
    Simis::Block uid(&data, TS::UiD);
    return data.getUint();
} // Block destructor seeks to the UiD block's end, even during unwinding.
```

`findToken()` is forward-only, does not recurse into payloads and skips
nonmatching siblings. It throws the same ParseError type for absence and bad
framing; there is no structured reason code or boolean optional-search API.
Do not catch every ParseError and pretend it simply meant “optional field absent.”

On match, findToken leaves off at the length field for historical callers.
Simis::Block rewinds four bytes, reads the matched header, seeks to payload and
installs its scope. Construction can throw before a Block object exists, so
its destructor cannot provide recovery for a failed construction.

Because a constructed Simis::Block seeks to its end during unwinding, capture
useful offsets/token context before risky operations if diagnostics need the
original failure location.

A separate `tryFindToken()`/`requireToken()` interface has been discussed, but
**neither function exists in the current implementation**.

## 8. Counts, arrays, strings and raw fields

`Simis::count()` defaults to 9 minimum bytes per item, the smallest empty-label
child block. Use it only after positioning at the schema's count field:

```cpp
data.skipLabel();                    // At a counted block's body.
const int count = Simis::count(&data, 4); // Example: count of uint32 indices.
for (int i = 0; i < count; ++i) {
    const quint32 index = data.getUint();
    // Use as an index, not as a token.
}
```

This assumes an active scope for that block. For actual child blocks the default
9-byte minimum can be suitable, but the schema still defines which children
count as entries. The helper neither validates every item nor imposes an
application memory budget or maximum renderer capacity.

For byte arrays already known to be positional:

```cpp
data.require(byteCount);
QByteArray bytes(reinterpret_cast<const char*>(data.data + data.off), byteCount);
data.off += byteCount;
```

For a counted payload string, prefer `QString value = data.readString()`.
The legacy `getString(start, end)` requires the caller to delete the returned
QString, does not advance the cursor, and drops code units whose **low byte**
is 0x0D. It is not a byte-faithful general UTF-16 decoder.
`Simis::Block::label()` currently uses this legacy method too, so its returned
label has the same filtering limitation. Raw label bytes remain accessible
between `body + 1` and `payload`.

Legacy text operations remain available: `isBOM()` probes without advancing,
`skipBOM()` advances two bytes only for a matching UTF-16LE BOM, `toUtf16()`
widens bytes rather than decoding general UTF-8, and `insertFile()` rebuilds
the remaining buffer around included text. They are not SIMIS binary framing
operations and were not redesigned into a universal text codec.

## 9. Saving: old manual fields versus an explicit block writer

FileBuffer remains a reader. Existing code uses QDataStream, and token migration
does not require changing that architecture. A manually sized empty-label UiD
block is still valid:

```cpp
out.setByteOrder(QDataStream::LittleEndian);
out << quint32(TS::UiD)
    << quint32(5)  // Body = one label-count byte + one uint32 value.
    << quint8(0)
    << quint32(42);
```

The complete block is 13 bytes. Its hex bytes are:

```text
6c 00 04 00  05 00 00 00  00  2a 00 00 00
UiD ID       body length  label payload=42
```

For variable strings, labels or nested children, computing the body from encoded
payload bytes is easier to review than maintaining several independent length
formulas. These are **example-local functions, not newly added library APIs**:

```cpp
QByteArray encodeFields(const std::function<void(QDataStream&)>& encode) {
    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out.setFloatingPointPrecision(QDataStream::SinglePrecision);
    encode(out);
    if (out.status() != QDataStream::Ok)
        throw std::runtime_error("Cannot encode SIMIS payload");
    return bytes;
}

void writeSimisString(QDataStream& out, const QString& text) {
    if (text.size() > 65535)
        throw std::length_error("SIMIS string exceeds uint16 code-unit count");
    out << quint16(text.size());
    for (QChar c : text)
        out << quint16(c.unicode());
}

void writeBlock(QDataStream& out, TS::TokenId id,
                const QByteArray& payload, const QString& label = {}) {
    if (label.size() > 255)
        throw std::length_error("SIMIS label exceeds uint8 code-unit count");

    const quint64 bodyBytes =
        1 + 2 * quint64(label.size()) + quint64(payload.size());
    // This example targets FileBuffer's int-sized storage, not all uint32 sizes.
    if (bodyBytes > quint64(std::numeric_limits<int>::max()) - 8)
        throw std::length_error("Block exceeds FileBuffer capacity");

    out.setByteOrder(QDataStream::LittleEndian);
    out << quint32(id) << quint32(bodyBytes) << quint8(label.size());
    for (QChar c : label)
        out << quint16(c.unicode());
    if (!payload.isEmpty()
            && out.writeRawData(payload.constData(), int(payload.size()))
                    != int(payload.size()))
        throw std::runtime_error("Cannot write SIMIS payload");
    if (out.status() != QDataStream::Ok)
        throw std::runtime_error("Cannot write SIMIS block");
}
```

Writer rules:

- Write IDs with an explicit `quint32` cast and little-endian stream order.
- Set `SinglePrecision` on streams that write float32 payloads; integer byte
  order alone does not select float width.
- Write uint8 labels and uint16 payload strings explicitly. Neither
  `out << QString` nor `out << QByteArray` matches these payload conventions.
- Parent payload lengths include complete serialized child blocks, including
  each child's 8-byte header.
- The example checks individual block size against FileBuffer's limit.
  A complete file/container must fit that limit as well.
- The buffered approach makes a temporary encoded payload copy. It is an
  explanatory implementation, not a performance benchmark or a requirement to
  buffer large production assets. A seekable writer can backpatch lengths, as
  long as it retains the same framing and checks errors.

## 10. Nested block write/read example

This writes a Static block containing UiD, Position and FileName properties and
a nonempty outer label. It then reads those same properties using the
recovery-first loop:

```cpp
QByteArray makeExampleObjectBlock() {
    const QByteArray children = encodeFields([](QDataStream& out) {
        writeBlock(out, TS::UiD, encodeFields([](QDataStream& payload) {
            payload << quint32(42);
        }));
        writeBlock(out, TS::Position, encodeFields([](QDataStream& payload) {
            payload << float(1) << float(2) << float(3);
        }));
        writeBlock(out, TS::FileName, encodeFields([](QDataStream& payload) {
            writeSimisString(payload, QStringLiteral("example.s"));
        }));
    });
    return encodeFields([&](QDataStream& out) {
        writeBlock(out, TS::Static, children, QStringLiteral("demo"));
    });
}

PreviewFields decodeExampleObjectBlock(const QByteArray& bytes) {
    auto data = copyBytes(bytes); // Fragment: starts at zero, no 32-byte header.
    const auto object = data->readBlock();
    if (object.id != TS::Static)
        throw FileBuffer::ParseError("Expected Static example block");
    data->off = object.payload;
    return readObjectChildren(*data, object.end);
}
```

A quick use:

```cpp
const QByteArray bytes = makeExampleObjectBlock();
const PreviewFields recovered = decodeExampleObjectBlock(bytes);
// recovered.uid == 42
// recovered.position == {1, 2, 3}
// recovered.fileName == "example.s"
// recovered.warnings is empty
```

This is a **block fragment**, not a complete route-ready world file: no SIMIS
file/subheader, Tr_Worldfile root or required application-specific surrounding
content has been added. For integration into a real file, retain that format's
validated header, parent schema and existing writer. Token registration and
block serialization alone do not implement a general binary world exporter.

To save only this demonstration fragment, for example as `example.block.bin`:

```cpp
void saveExampleFragment(const QString& path) {
    const QByteArray bytes = makeExampleObjectBlock();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw std::runtime_error(file.errorString().toStdString());
    if (file.write(bytes) != bytes.size())
        throw std::runtime_error(file.errorString().toStdString());
    if (!file.commit())
        throw std::runtime_error(file.errorString().toStdString());
}
```

QSaveFile stages output and commits only after a successful write. The example
does not modify any existing route. This does not add SIMIS compression:
compression belongs to the outer-file writer, not to FileBuffer or each child
block. `ReadFile::read()` handles the existing compressed-input wrappers.

## 11. Integration checklist and known limitations

Before converting another consumer:

- Identify its file header, parent boundaries, counted collections and
  positional fields. Replace token identities without reinterpreting scalars.
- Decide which omissions are ordinary and which actually prevent use of a unit.
  Optional absence must not become a whole-file error.
- Put scopes around payload reads; distinguish ScopedLimit's bound restoration
  from Simis::Block's automatic cursor seek.
- Stage multi-value fields until fully read. Catch recoverable failures at the
  smallest useful unit and preserve successfully decoded independent content.
- Preserve diagnostics and an incomplete-load indication without assuming
  that “loaded for viewing” implies “safe for lossless save.”
- Check memory ownership. Buffer scopes do not roll back allocations or clean
  consumer-owned arrays/GL resources.
- Do not assume all legacy calls are safe: raw getters outside scopes and
  public pointer/cursor manipulation still exist.
- Test ordinary files as well as malformed fixtures and measure performance
  before claiming no regression.

The current implementation does not provide automatic recovery, a lossless
unknown-block document model, optional-search status, full-file validation,
streaming/memory-mapped storage, 64-bit file offsets, a general writer or an
old-versus-new parser performance result.

At the documented commit, Tile's binary loader rolls back newly parsed objects,
SFile marks the shape failed, and TFile reports load failure on ParseError.
Those are consumer decisions, **not necessary consequences of checked reads**.
This documentation adds no runtime fix for that policy.

### 11.1. Status and follow-up TODOs

This is the canonical parser follow-up checklist, not authorization to implement
everything in it. The current request changes only the two QuadTree writer IDs
(and their include), plus documentation. Do not silently broaden the code change.

Completed / retained scope:

- [x] Introduce complete unsigned native IDs and remove the world-file offset.
- [x] Document actual API behavior, examples, consumers and current limits.
- [x] Replace the two QuadTree writer token literals with enum names while
  leaving its legacy reader and wire bytes unchanged.

Required before accepting the parser integration as a recovery-first replacement:

- [ ] Replace whole-file rejection/rollback with recovery at the smallest safe
  field/object/primitive/sub-object boundary. Retain usable independent content;
  do not remove bounds checks or render structures with invalid dependencies.
- [ ] Review every required-child assumption. Treat optional absence normally;
  distinguish missing required data from damaged framing. An optional-search
  result or explicit required-search API may help, but simply catching all
  ParseError exceptions as “not found” is not a solution.
- [ ] Keep partial-load diagnostics/state separate from permission to save.
  Recovery for viewing must not silently overwrite an original with incomplete
  or dropped content.
- [ ] Audit allocations and state on partial failure, including shape arrays/GL
  resources and terrain data. A block scope is not an ownership rollback.
- [ ] Compare representative existing binary/Unicode files against the previous
  behavior, including unusual valid variants and partially damaged assets.
  Add regression fixtures for usable content surviving local failures.
- [ ] Measure old/new loading time and memory on representative files. The
  token-table/fixture tests are not a parser performance benchmark.

Shared helper issues to resolve in a separately scoped follow-up:

- [ ] Make label decoding byte-faithful: Simis::Block::label() currently inherits
  getString()'s low-byte-0x0D filtering.
- [ ] Make FileBuffer ownership/copy semantics explicit; ordinary implicit
  copying is unsafe. Preserve callers deliberately using the pointer-based
  deep-copy constructor or migrate them explicitly.
- [ ] Audit bounds for touched positional readers and manual skips. Numeric
  getters outside ScopedLimit remain unchecked; any broader raw/text-reader
  conversion needs its own scope and compatibility tests.
- [ ] Improve error context so callers can distinguish failure categories and
  report the offending ID/offset without relying on a cursor changed during
  exception unwinding.

Existing codec limitations / optional future work, not requirements of token migration:

- [ ] Complete specialized binary SoundRegion fields if requested.
- [ ] Add Ruler/ShapeTemplate/ORTS extension binary codecs or a general binary
  world writer only as separate features. Registration alone is not support.
- [ ] Extend shape primitive/controller or multiple-LOD-control support only
  when requested; these renderer limitations were not solved by token migration.
- [ ] Consider checked QuadTree header parsing only under a separate request.
  The minimal writer cleanup does not authorize replacing its reader.
- [ ] Consider lossless unknown-block retention or a reusable block writer only
  if a consumer needs them; the guide's writer functions remain examples.

No Windows-host access, merge, publication or rewriting of existing route/test
files is authorized by this checklist.

## 12. Verification and further reading

The existing automated suites cover checked framing, namespaces, labels and
truncation, with generated terrain/world/shape fixtures. See the
[token-ID and parser implementation report](../tasks/core/native-token-ids-and-binary-parser-implementation.md) for
commands and the limits of that earlier testing. They do not establish that
every valid historical file variant is accepted or that recovery is complete.

The standalone C++ examples in this guide were compiled against the actual
FileBuffer/ReadFile/TS implementation on Linux: **23 checks passed**. The
temporary example-check harness is not a new committed test-suite target.
The check exercised nested
write/read, the UiD golden bytes, preservation of earlier fields after a bad
payload, continuation to later siblings, stopping at an invalid child header,
required-search absence, label/string size limits, and fragment saving.
This is an example/API check, not broad route compatibility testing.

To compile these functions with a small `main()` of your own, assemble the
includes and function examples into `examples.cpp`, then run from the TSRE root:

```sh
g++ -std=c++17 -fPIC -I src examples.cpp \
    src/tsre/fileFunctions/FileBuffer.cpp \
    src/tsre/fileFunctions/ReadFile.cpp src/tsre/fileFunctions/TS.cpp \
    $(pkg-config --cflags --libs Qt6Core) -o filebuffer-examples
```

`-fPIC` is needed with the Qt library in the verification environment; omitting
it caused a protected-symbol/copy-relocation linker error. The successful build
still emitted existing Qt/GCC header warnings and the legacy unused-variable
warning in `insertFile()`; no library source was changed to compile the guide.

Related references:

- [Token allocation and API overview](native-token-ids.md)
- [Native token-ID implementation plan](../tasks/core/native-token-id-migration.md)
- [Token-ID and parser implementation report](../tasks/core/native-token-ids-and-binary-parser-implementation.md)
- [Core infrastructure task index](../tasks/core/README.md)
