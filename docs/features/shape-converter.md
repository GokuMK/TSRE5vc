# Shape saving and conversion

The standalone Shape Viewer opens MSTS `.s` files with the complete
`SFileComplex` backend. Its **Save** action preserves the source encoding and
compression. **Save As** can preserve those settings or choose Unicode/binary
and compressed/uncompressed output. Embedded shape previews continue to use
the application's configured shape backend, which defaults to `SFileLegacy`.

Shape writes are atomic and are parsed and re-encoded for validation before an
existing file is replaced. Saving does not rewrite the adjacent `.sd` file.

The same document codec is available without opening a GUI:

```text
TSRE5vc.exe --shapeconv INPUT.s -o OUTPUT.s [options]
```

Options:

```text
-i, --input PATH
-o, --output PATH
-f, --format preserve|unicode|binary
-c, --compression preserve|compressed|uncompressed
-w, --overwrite
```

Input can be supplied positionally or with `-i`/`--input`. Format and
compression default to `preserve`. Existing outputs are rejected unless
`-w`/`--overwrite` is present.

Examples:

```text
TSRE5vc.exe --shapeconv wagon.s -o wagon-text.s -f unicode
TSRE5vc.exe --shapeconv -i wagon.s -o wagon-bin.s -f binary -c compressed
```

Use `--shapeconv --help` for command-specific help. The mode name deliberately
has no short alias.
