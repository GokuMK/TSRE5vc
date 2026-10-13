# miniaudio

Audio playback library used by TSRE's sound module (`src/tsre/sound`).

- Version: 0.11.25 (released 2026-03-03)
- Source: https://github.com/mackron/miniaudio, tag `0.11.25`
  (archive SHA-256 `b900edcffe979816e2560a0580b9b1216d674b4f17fbadeca8f777a7f8ab0274`)
- File: `miniaudio.h` unchanged from the tag
  (SHA-256 `ac7af4de748b7e26b777f37e01cee313a308a7296a3eb080e2906b320cc55c89`)
- Licence: public domain (Unlicense) or MIT No Attribution, see `LICENSE`

TSRE compiles the implementation once in `src/tsre/sound/Miniaudio.cpp`
with the options in `src/tsre/sound/MiniaudioConfig.h` (no decoders, no
encoders, no resource manager: TSRE reads its own wave files). To update,
replace `miniaudio.h` with the new release's and update this file.
