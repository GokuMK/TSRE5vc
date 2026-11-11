# stb_vorbis

Ogg Vorbis decoder used by TSRE's sound module (`src/tsre/sound`) to read
`.ogg` sounds (MaSzyna's, task maszyna 05).

- Version: 1.22
- Source: https://github.com/nothings/stb, commit
  `1ee679ca2ef753a528db5ba6801e1067b40481b8` (2021-07-12), file
  `stb_vorbis.c`
- File: `stb_vorbis.c` unchanged from the commit
  (SHA-256 `4c7cb2ff1f7011e9d67950446b7eb9ca044f2e464d76bfbb0b84dd2e23e65636`)
- Licence: MIT or public domain (Unlicense), see `LICENSE`

TSRE compiles the implementation once in `src/tsre/sound/StbVorbis.cpp`
with the options in `src/tsre/sound/StbVorbisConfig.h` (no stdio, no
push-data API: TSRE reads the file and decodes it from memory). To update,
replace `stb_vorbis.c` with the new commit's and update this file.
