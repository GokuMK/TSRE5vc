# Route Editor server/client rework

Status: deferred. The multiplayer/server subsystem is out of date and needs a
separately scoped redesign. These findings do not block native token IDs, but
they must not be lost as incidental parser TODOs. Do not start implementation
until the user approves a dedicated task.

## Issues confirmed during native-token parser review

### Terrain update failure is not propagated

`TFile::load()` returns `false` for malformed input, but
`Terrain::loadTFile()` and the RAW/F equivalents expose `void` APIs.
Consequently, higher-level message handlers cannot tell whether an update was
accepted:

- the server may call `setModified()` and rebroadcast a rejected or partially
  applied terrain update;
- the client may call `refresh()` after a rejected terrain descriptor or RAW
  update;
- `TFile::load()` mutates its live object while parsing, so failure is not an
  atomic rollback;
- `TerrainClient::loadTFile()` replaces its descriptor with `new TFile()`
  without releasing the previous descriptor; repeated or rejected responses
  can leak and leave a partial replacement installed.

The future API should return a structured result (at minimum `bool`) through
every layer. Modify, refresh and broadcast only after complete validation and
successful state publication.

### WebSocket buffer ownership is invalid

Client and server message handlers construct
`FileBuffer((unsigned char*)message.data(), message.size())`. That constructor
owns its pointer and its destructor uses `delete[]`, but `QByteArray::data()` is
borrowed storage. The handlers currently avoid the invalid deletion by never
deleting the `FileBuffer`, leaking one wrapper per message and leaving a
dangling borrowed pointer after the slot returns.

The rework should provide an explicit borrowed-span constructor/view or make a
compatible owned copy, then use RAII. FileBuffer copy and ownership semantics
are also tracked in the shared FileBuffer follow-up list.

### Binary envelope framing is incomplete

The historical payload-size DWORD is written as zero and ignored. The native
token helper validates the marker, token and minimum fixed envelope, but not an
independently declared payload length. Nested T files validate their own SIMIS
framing; RAW/F and QuadTree messages still depend on message-specific checks.

A redesigned protocol should define a real payload length, reject trailing and
truncated payloads consistently, cap message sizes, and return useful errors to
the sender rather than only logging locally.

### Protocol compatibility is implicit

Native IDs intentionally make the eight binary terrain/QuadTree messages
incompatible with legacy peers. The current decoder recognizes legacy numeric
IDs and reports that client and server must be upgraded together, but there is
no negotiated protocol version or capability handshake.

The future protocol should advertise a version/capabilities during login and
reject incompatible peers before exchanging mutable route state. Supporting
legacy peers is optional; silent ID translation inside nested files is not.

### Integration coverage is missing

Generated tests cover native envelope bytes and nested terrain parsing, but no
live matched client/server route session was exercised. There are no tests for
rejection without mutation/rebroadcast, disconnects during large transfers,
duplicate/out-of-order updates, or mixed-version peers.

## Suggested acceptance criteria for the future task

1. Message buffers have explicit safe ownership and no per-message leak.
2. Every update is validated before publication and reports success/failure.
3. Failed updates do not mutate, refresh, mark modified or rebroadcast state.
4. Envelope length/version/capability rules are explicit and tested.
5. A live or loopback integration test covers T/RAW/F and detailed/distant TD
   exchange, malformed payloads, disconnects and matched/mismatched versions.
6. Authentication/authorization and concurrent-edit conflict behavior receive
   a separate security and consistency review as part of the redesign.
