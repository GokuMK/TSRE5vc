# Qt-free ACE codec build

`AceCodec.cmake` supplies `tsre_add_ace_codec(target)`. It compiles TSRE's
`AceDocument.cpp` and `DxtCodec.cpp` unchanged, with the private `qt_compat`
headers and bundled miniz. Consumers are `extra/ace-thumbnails` and
`extra/gimp-ace`; TSRE's normal Qt build does not use this helper.

The compatibility layer implements only the Qt subset used by those sources.
Byte arrays and containers own their data and copy eagerly. Strings carry codec
errors. `qCompress` implements Qt's four-byte length prefix plus zlib stream using
miniz, and throws on failure. `QFile` and `QSaveFile` deliberately fail: consumers
read/write byte buffers through their native APIs.

Never combine these replacement Qt types and actual Qt objects in the same
binary. A future static Qt backend can replace the helper's include paths and
compression source with Qt Core linkage, keeping the consumer's ACE API calls.
The thumbnail project's optional Qt parity test verifies both implementations
against the same corpus.
