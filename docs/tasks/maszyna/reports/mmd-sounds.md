# MaSzyna sounds in `.mmd`: semantics and the pack

Review for task maszyna 05 (2026-11-10). Part A: how MaSzyna reads, places
and plays a vehicle's and a cab's sounds, from its code (`../maszyna` at
66a940d4). Part B: what the pack (`~/ORTS/maszyna`) holds, measured with
MaSzyna's own reading rules (includes with their (pN), random sets, both
syntaxes). `(unsure)` marks inferences not fully checked; the file lookup,
the table position, the crossfade, the soundproofing lookup and the cab
control sounds were checked against the code again (2026-11-10).

# Part A: semantics

Main files:
- `audio/sound.h`, `audio/sound.cpp`: `sound_source`. It parses a sound, decides what to play, and holds the soundproofing logic.
- `audio/audiorenderer.{h,cpp}` and `audio/audiorenderer_extra.h`: the OpenAL layer (distance, gain, culling, source pool).
- `audio/audio.{h,cpp}`: buffers, file lookup, decoding.
- `vehicle/DynObj.{h,cpp}`: per-vehicle sounds. Loading is in `LoadMMediaFile`, runtime in `RenderSounds` and `powertrain_sounds::render`.
- `vehicle/Train.{h,cpp}`: cab sounds of the player's train (`LoadMMediaFile`, `InitializeCab`, `update_sounds`).
- `vehicle/Gauge.cpp` and `vehicle/Button.cpp`: sounds on cab controls and lamps.

---

## 0. The .mmd file around the sounds

- **Wrapper.** `TDynamicObject::LoadMMediaFile` (DynObj.cpp:5181) opens the file through a synthetic `include <Type>.mmd <asName> <TypeName> <skin> end` (DynObj.cpp:5200-5207). Inside the .mmd and its includes, `(p1)`, `(p2)` and `(p3)` expand to those three values.
- **Random includes.** `include [ a.inc b.inc ... ] end` includes one file picked at random (`parser.cpp:564-593`, `deserialize_random_set`). The ET22 uses this for horns and for the compressor/converter set. The pick is made per vehicle instance at load time, using `Global.random_engine`.
- **Top-level sections parsed by DynObj.** These are `models: ... endmodels`, `locations: ... endlocations`, `sounds: ... endsounds` and `internaldata:`. `internaldata:` runs to the end of the file (DynObj.cpp:6622-6978, loop `while( token != "" )`).
- **Parsing is a token scan.** Each section reads tokens one at a time. A recognised `key:` is handled; anything else is skipped one token at a time.
- **Two parsers read `internaldata:`.** DynObj reads its keys (relays, couplers, line breaker, wipers and others). `TTrain::LoadMMediaFile` re-reads the file, skips to `internaldata:` (Train.cpp:10243-10248), and picks out its own cab sound keys (section 5). Each parser ignores the other's keys.
  - Hazard (pack seems clean): a value inside a block that happens to be one of the other parser's keys would be taken as a key. The worst case is a `soundproofing:` inside a Train-only block. DynObj would read it as the 30-number vehicle table. In the pack, every `{ ... soundproofing: ... }` block belongs to a DynObj key.
- **Order matters.**
  - `locations:` must come before `sounds:`. Door, motor and bogie sound templates are copied into entries that `locations:` created (DynObj.cpp:6001-6018, 6060-6075, 6310-6388, 6422-6445). Pantograph entries come from the `models:` animation setup (DynObj.cpp:5660), which also precedes `sounds:`.
  - Inside a sound block, `pitch<N>:` must come after its `sound<N>:` (section 1.3).
- **Comment lines.** `//` lines are comments (`//switch: ...` in the ET22).

---

## 1. Parsing a `sound_source` (every syntax)

Entry point: `sound_source::deserialize(cParser&, sound_type Legacytype, int Legacyparameters = 0, int Chunkrange = 100)` (sound.cpp:40-145). A string overload exists (sound.cpp:35-38) and is used for built-in defaults such as `"lomotpodkucia 0.23 0.0"`.

The first token is read lowercased with separators `"\n\r\t ,;"` (sound.cpp:46). If it is `{`, the block form follows; anything else is the legacy form (sound.cpp:47 / 95).

### 1.1 Legacy single-line form (sound.cpp:95-132)

**Token order:**
1. **Samples** (sound.cpp:101-117):
   - `sound_type::single`: one sample, the *main* sound.
   - `sound_type::multipart`: three samples in the order **begin, main, end**.
   - Each sample is one token or a random set `[ a b c ]`. One set member is picked at load time (`deserialize_random_set`, `utilities.cpp:598-622`). `[`, `]` and the members must be whitespace-separated; `\` becomes `/`.
2. **Optional numbers**, decided per key by the caller's `Legacyparameters` bit mask (`sound.h:28-32`), always in this order:
   - `range` (bit 0x1): 1 float → `m_range`.
   - `amplitude` (0x2): 2 floats → `m_amplitudefactor`, then `m_amplitudeoffset`.
   - `frequency` (0x4): 2 floats → `m_frequencyfactor`, then `m_frequencyoffset`.

Each number is read with `getTokens(n,false)`. A number left out is not skipped: the next key gets eaten as the number. The token count is fixed per key.

The `Legacytype` and `Legacyparameters` for every key are in section 4 (vehicle) and section 5 (cab). Worked examples:

| Line | Meaning |
|---|---|
| `ventilator: 697_et22-rvent.wav 150.0 2.2 0.2 0.7 0.4` | single, range\|amp\|freq: file, range=150, ampfactor=2.2, ampoffset=0.2, freqfactor=0.7, freqoffset=0.4. Then ampfactor and freqfactor are divided by `RVentnmax` (DynObj.cpp:6084-6091). |
| `wheel_clatter: 70 -7.0 a.wav -5.25 b.wav ... end` | Not a sound_source line (DynObj.cpp:5927-5949). The first number is the **range** of every axle emitter (`sound_source{external, 70}`). Then come pairs `<axle z> <sound>`, where the sound is single with no numbers (it may be a `{}` block or a `[ ]` set). The z is negated: a negative value means ahead of centre. The list ends with `end`. |
| `sand: a.wav b.wav c.wav 15` | multipart + range: begin=a, main=b, end=c, range 15. |
| `pantographup: x.wav` | single, no numbers. |
| `derail: derail.wav 200` | single + range. |
| `brakesound: 697_brake.wav 0.2 0.0 1.5 0.2` (cab, internaldata) | single, amp\|freq: af=0.2, ao=0.0, ff=1.5, fo=0.2. Then ff is divided by (1+Vmax) (Train.cpp:10290-10293). |
| `airsound2: [1007]fv4a_luz.wav 0.05 -0.01` | single + amp: af=0.05, ao=-0.01. `[1007]fv4a_luz` is a literal file name: only a lone `[` opens a set. |
| `slipperysound: brake-slipp.wav 2.0 0.2` | single + amp: af=2.0, ao=0.2, then af /= (1+Vmax). |

### 1.2 Block form `{ key: value ... }` (sound.cpp:47-94, 148-274)

- **Keys** are read lowercased with separators `"\n\r\t ,;[]"`, so `[ ]` around vectors are optional.
- **Unknown keys** are skipped, but their value is not consumed: it is then read as the next key, which is normally harmless.
- **The block ends** at `}` or at end of file.
- **No legacy numbers** are read after a block.

| Key | Value | Effect / default |
|---|---|---|
| `soundmain:` | sample or `[ set ]` | Main sound (sound.cpp:156-158). |
| `soundbegin:` | sample or set | Opening part, played once before the main sound (162-164). |
| `soundend:` | sample or set | Closing part, played after stop (165-167). |
| `soundset:` | `[ b1\|m1\|e1, b2\|m2\|e2, ... ]` or a single `b\|m\|e` | One whole triple is picked at random at load, then split on `\|` into begin/main/end (sound.cpp:159-161, 277-286). The commas are separators. |
| `sound<N>:` | sample or set | Entry of the **sound table** (combined sound): sample active around value N. N is an integer and may be negative (`sound-2:`). The number is parsed from the first digit or `-` in the key (186-199). Any key starting with `sound` and containing a digit lands here. |
| `pitch<N>:` | float | Pitch the table entry with threshold N was recorded at (214-228). Values <= 0 become 1. It must follow its `sound<N>:`, because it searches the entries loaded so far; otherwise it is silently dropped. Default 1. |
| `crossfade:` | int 0-100 | Percent of the previous entry's span used for crossfading (229-234). Default 0. It is read into an `int`. (Quirk: the call passes the separator string as the `ToLower` bool, which is harmless.) |
| `placement:` | `internal` / `engine` / `external` / `custom` / `general` | sound.cpp:235-248. Other words, including the pack's typos `externa` and `interal`, keep the default. `external_ambient` cannot be set here. The default comes from the C++ member (sections 4 and 5). |
| `offset:` | `[x y z]` | Position in the vehicle frame: x=left, y=up, z=front (249-256, `location()` 903-915). Default (0,0,0). If it stays at (0,0,0), most owners place the sound automatically (section 4.6). |
| `soundproofing:` | 6 floats `[p1 ... p6]` | Custom per-listener-location factors. Each value v becomes `sqrt(clamp(v,0,1))`; `-1` is kept as is (168-185). Section 2.7 gives the column meaning and the `-1` problem. |
| `range:` | float | Audible range in metres (257-271). The default depends on the sound (section 4). `-1` means global (no position, no culling); other negative values are described in 2.6. |
| `amplitudefactor:` / `amplitudeoffset:` | float | Helpers for the owner's gain formula. Defaults 1 / 0. **sound_source never applies them itself** (2.5). |
| `frequencyfactor:` / `frequencyoffset:` | float | Same, for the pitch formula. Defaults 1 / 0. |
| `pitchvariation:` | float 0-1 | Per-emitter random pitch multiplier, fixed at load. v becomes a uniform pick in `[1 - v/2, 1 + v/2]`; `0` means exactly 1.0 (200-207). If the key is absent, the first `play()` picks a uniform value in `[0.975, 1.025]` (366-368). Used about 713 times in the pack. |
| `startoffset:` | float 0-1 | Start the main sample at this fraction of its length (208-213; applied in `audiorenderer_extra.h:26-38` and `audiorenderer.cpp:96-109`). Unused in the pack. Code sets it for motors, blowers and bogies (section 4). |

**After parsing a block:**
- **Table sorting and ranges** (sound.cpp:53-93). The table is sorted by threshold, then each entry gets:
  - `fadeout[i] = threshold[i+1]`.
  - For the last entry, `fadeout = max(Chunkrange, threshold_last)`. `Chunkrange` defaults to 100; it is `Vmax` for the running-noise sounds (`outernoise:` and the cab `runningnoise`/`resonancenoise`/`windsound`/`huntingnoise`).
  - `fadein[0] = max(0, threshold[0])`.
  - `fadein[i] = threshold[i] - crossfade% × (threshold[i] - threshold[i-1])`.
  - `m_soundchunksempty` is false if any entry has a buffer.
- **Repeated samples** (sound.cpp:136-142). If begin (or end) is the same buffer as main, that bookend is dropped. This applies to both forms.

### 1.3 Other ways sounds are defined

These share the same `sound_source` syntax for their values.
- **Cab controls** (`TGauge`) and **lamps** (`TButton`) have their own keys in the cab section: section 5.4.
- **`announcements: { near_stop: s stop: s next_stop: s destination: s chime: s soundproofing: [6] }`** (DynObj.cpp:6459-6521). Each value is single and may be a `{}` block. The block-level `soundproofing:` (square-rooted) goes to every announcement that has none of its own. Unknown keys consume one value.
- **Vehicle soundproofing table** `soundproofing:` in `internaldata:`: 30 floats, see 2.7.

---

## 2. Playback

### 2.1 `play` / `stop` and the flags (sound.h:34-38, sound.cpp:344-507)

The owner code calls `play(flags)` every frame while the sound's condition holds, and `stop()` when it ends.
- **`play()` with no flags.** One-shot. If the sound is already playing and has **no begin part**, another instance is spawned, so instances overlap (sound.cpp:405-411). Used by clatter, relays, coupler attach and similar.
- **`exclusive`.** At most one instance. Calling `play(exclusive)` every frame restarts the sample as soon as the previous one finishes. Load exchange, door sounds and `dieselinc` rely on this; the door code limits the window (door position < 0.5) to avoid repeats.
- **`looping`.** The main sample loops (sound.cpp:548, 590). Looping sounds are always also exclusive in vehicle code.
- **`stop(Skipend=false)`** (489-507):
  - Sets `m_playbeginning = true` and clears the flags.
  - If the sound was playing, it requests an immediate kill of all non-end instances (`m_stop`). The main loop is cut at once, not at the end of the loop.
  - It also queues the **end** part unless `Skipend` is set. At most 2 end instances can exist (`playing < 2`).
  - `is_playing()` ignores the end part (864-878).
- **Out-of-range starts.** A `play()` dropped for distance (2.6) still sets `m_playbeginning = false`. So when the listener comes in range later, the loop starts **without** its begin part (sound.cpp:355-363).
- **Shared state.** `gain()` and `pitch()` set one shared state per emitter. All its live instances, including begin/end parts of basic sounds, use the current values (`update_basic` syncs `m_properties` whichever part is playing, 573-575).
- **Owner removed.** If the owner vehicle is disabled, the emitter stops (511-518).
- **Captions.** When an instance starts with gain > 0 and its buffer has a caption, the caption goes to the UI transcripts (931-941). The caption file is `<sample path without extension>-<lang>.txt` (`audio.cpp:213-224`).

### 2.2 Basic sounds: begin, main, end

A basic sound is one with a `main` sample.
- **Start** (play_basic, sound.cpp:391-412). The first `play()` queues **[begin, main] in one OpenAL source** as a buffer queue, if begin exists and `m_playbeginning` is set. Otherwise it queues main alone.
- **Switch to main** (`openal_source::update`, audiorenderer.cpp:85-110 → `update_basic` sound.cpp:539-550). When begin finishes, the renderer unqueues it. The controller then sets looping on the main buffer if the `looping` flag is on.
- **Stop.** `stop()` kills begin/main immediately and starts **end** as a separate source. End plays to completion and is not killed by `m_stop` (552-559).
- **Start offset.** `start()` only applies when main is the only or last buffer (`audiorenderer_extra.h:26-38`, `audiorenderer.cpp:96-109`). The sample offset is `start × (bytes/2)`, valid because all buffers are mono 16-bit.

### 2.3 Sound tables (combined sounds)

A sound with `sound<N>` entries and no `soundmain` is "combined" (`is_combined()` sound.cpp:882-885).
- **Control value.** The owner passes the controlling value through `pitch(x)`. For combined sounds `pitch()` sets the table position, not the playback pitch. The position is (`compute_combined_point`, sound.cpp:465-473):
  `point = (x <= 1 ? clamp(x, 0, 0.99) : max(0, x)) × 100`
  Owners therefore pass `value × 0.01` to address the table in "value" units (km/h, rpm, notch...). The 0.99 clamp means values in (0.99, 1] map to 99.
- **Which entries play** (play_combined, sound.cpp:415-461). Entry i plays while `fadein[i] <= point < fadeout[i]`. Each active entry is its own looping OpenAL source.
  - When looping or exclusive, an entry that is already playing is not started again (427-431). In a crossfade zone, two neighbours play together.
  - In `update_combined` (668-680), an exclusive/looping entry whose range no longer contains the point is stopped.
  - **Begin part:** queued only together with entry 0, when begin is not already playing and `m_playbeginning` is set (433-459). If the first position is above entry 0's range, begin never plays.
  - **End part:** played by `stop()` as for basic sounds.
  - On the switch from begin to entry 0, looping is forced on (638-643).
- **Playback pitch of an entry** (update_crossfade, sound.cpp:742-791). Let `p[i]` be `pitch<i>` (default 1) and `t[i]` the thresholds.
  - Below its threshold (`fadein[i] <= point < t[i]`), for i > 0: `pitch = lerp(p[i-1]/p[i], 1, clamp((point - t[i-1]) / (t[i] - t[i-1]), 0, 1))`.
  - At or above its threshold, when there is a next entry: `pitch = lerp(1, p[i+1]/p[i], clamp((point - t[i]) / (t[i+1] - t[i]), 0, 1))`.
  - The last entry at or above its threshold: `pitch = 1`.
  - Begin/end parts play at pitch 1 (744-748).
  - So `pitch<N>` is the control value the sample was recorded at, and the sample is resampled linearly towards its neighbour's recording point. With no `pitch<N>` values, a combined sound never changes pitch.
  - Example (ET22 `tractionmotor`): with `sound65 pitch65:60` and `sound180 pitch180:160`, entry 65 plays from rate 1.0 at point 65 up to 160/60 = 2.67 at point 180.
- **Crossfade gain** (sound.cpp:792-829). This only applies when `crossfade > 0`; it multiplies the gain the owner set.
  - Fade-in, entry i > 0, `point` in [fadein, t[i]): `g = (point - fadein[i]) / (t[i] - fadein[i])`, then `gain *= g / (1 - 0.57 × (1 - g))`.
  - Fade-out, entry i not last, `point` in (fadein[i+1], t[i+1]): `g = (point - fadein[i+1]) / (t[i+1] - fadein[i+1])`, then `gain *= (1 - g) / (1 - 0.57 × g)`. These curves approximate a log/equal-power fade.
  - Outside these zones the factor is 1. The fade-in branch returns early, so an entry never gets both factors.
- **Tables without looping.** With non-looping, non-exclusive calls (relay, shunt field, brake cylinder), each call plays the entry whose range contains the point as a one-shot. This turns a table into "one sample per notch"; the ET22 `relay` table has `sound0 ... sound47`, one per resistor position.

### 2.4 When random choices are made

Every `[ a b c ]` set and every `soundset:` triple is picked **once, when the definition is parsed** (`deserialize_random_set` uses `Global.random_engine`). The same applies to `include [ ... ]`.
- Templates copied to several emitters (doors, motors, pantographs, couplers) share that one pick.
- Different vehicle instances of the same type parse separately, so they can differ.
- Nothing is re-picked during playback.

### 2.5 Gain and pitch formulas

`sound_source` does not apply `amplitudefactor/offset` or `frequencyfactor/offset`. **Each owner computes the values and calls `gain(g)` / `pitch(p)`.** The common pattern is:
- `gain = amplitudeoffset + amplitudefactor × X`
- `pitch = frequencyoffset + frequencyfactor × Y`

X and Y differ per sound and are listed in section 4. Many owners pre-divide the factor at load, for example by `1+Vmax` or by `nmax×60 + Power×3`.

When no runtime gain is set, gain stays at 1.0 (`sound_properties` default, audiorenderer.h:45-52). So for many on/off sounds, af/ao do nothing; section 4 lists which sounds use them.

**What reaches OpenAL** (`openal_source::sync_with`, audiorenderer.cpp:123-207):
- Gain:
  `AL_GAIN = clamp(gain, 0, 2) × crossfadefactor × soundproofing × categoryvolume × rangefactor`
  - `gain()` clamps to [0, 2] (sound.cpp:834-838).
  - `rangefactor` is 1 inside the nominal range (2.6).
  - The listener gain is `Global.AudioVolume` (audiorenderer.cpp:426).
  - OpenAL itself may clamp a source's attenuated gain to `AL_MAX_GAIN` (default 1); this is OpenAL behaviour, not MaSzyna code (unsure).
- Pitch:
  `AL_PITCH = clamp(pitch × pitchvariation, 0.1, 10)` (audiorenderer.cpp:200-203). For combined sounds, `pitch` here is the entry's relative pitch from 2.3, not the control value.

### 2.6 Range, attenuation and culling

| Setting | Code | Effect |
|---|---|---|
| Distance model | audiorenderer.cpp:327-328 | `AL_INVERSE_DISTANCE_CLAMPED`, doppler factor 0.25. |
| Per source | audiorenderer.cpp:211-224 | `AL_REFERENCE_DISTANCE = range/16` and `AL_ROLLOFF_FACTOR = 1.75`. Negative range uses 5/16. On a gain or listener-location change, the reference distance is re-set to `range/16 × soundproofing` (audiorenderer.cpp:171-181). Soundproofing therefore also shrinks the reference distance; the `EU07_SOUND_PROOFINGUSESRANGE` macro is defined but not tested anywhere. |
| Beyond nominal range | audiorenderer.cpp:182-198 | When distance d > range: `rangefactor = lerp(1, 0, clamp((d² - range²) / (0.75·range)², 0, 1))`. Silence is reached at d = 1.25 × range. |
| Start culling | sound.cpp:355-363 | `play()` is ignored when the distance to the camera exceeds `min(2750, |5 × range|)`. Range `-1` is never culled. |
| Running culling | audiorenderer.cpp:142-152 | The renderer stops a source beyond `min(3000, 7.5 × |range|)`. Multi-buffer sources (begin+main queued) use 3000 m. |
| `range == -1` (global) | audiorenderer.cpp:142, 153-162, 176-179 | Positioned on the listener (0,0,0), no culling, no fade, reference distance 5/16. |
| Other negative range | — | Positioned on the listener (non-positional), but the fade and culling still use the real distance with \|range\|. When range < -1, soundproofing is also × 0.4 (sound.cpp:1001-1004). Used by load exchange and PA announcements (range = -L/2). |
| Velocity / doppler | audiorenderer.cpp:131-138, 442-467 | Source velocity is the position change divided by dt, capped at 250 km/h. Negative-range sounds take the listener's velocity (no doppler). Listener velocity is zeroed on camera jumps (free-fly toggle, window toggle, cab change, jump > 100 m). |

### 2.7 Placement and soundproofing (sound.cpp:957-1008, DynObj.h:588-596, 826-827)

**Constants** (sound.h:49-52): NONE = 1.0, SOME = √0.65 ≈ 0.806, STRONG = √0.20 ≈ 0.447, VERYSTRONG = √0.01 = 0.1.

**Placement enum** (sound.h:40-47): general = 0, internal = 1, engine = 2, external = 3, external_ambient = 4, custom = 5. The vehicle table row is `placement - 1`.

**Listener location.** First `occupiedcab` is computed from the camera (sound.cpp:961-968):
- `CabWindowOpen` gives 2.
- Otherwise, in free-fly: 3 if the camera is bound to a vehicle (`Global.pCamera.m_owner`; external views consist-front/rear/bogie, drivermode.cpp:1120-1190), else 4.
- Otherwise it is the listener vehicle's `CabOccupied` (-1, 0 or 1).

**Column index into a 6-entry table** (`listenerlocation + 1`):

| Col | listenerlocation | Situation |
|---|---|---|
| 0 | -1 | In this vehicle, rear cab |
| 1 | 0 | In this vehicle, "cab 0" (engine room or a vehicle without an active cab) |
| 2 | 1 | In this vehicle, front cab |
| 3 | 2 | Cab window open or mirror view |
| 4 | 3 | External camera bound to this vehicle |
| 5 | 4 | Free camera, **or the listener is in any other vehicle** |

**Factor for a sound owned by vehicle V** (sound.cpp:980-999):
1. `general` placement gives **1.0, always**. There are no steps 2-3, and the listener's vehicle does not reduce it either.
2. Otherwise:
   - `listenerlocation = (V == listener vehicle) ? occupiedcab : 4`.
   - `factor = sound's own soundproofing:[col]` if the sound has one, else `V.m_soundproofing[placement-1][col]`.
3. If the listener is inside another vehicle L (camera owner ≠ V, not free camera), multiply by L's **external** row at L's own column: `factor *= L.m_soundproofing[external-1][occupiedcab+1]`. This is the "outside → into my cab" stage.
4. If range < -1, also × 0.4.

The result is cached until the listener's vehicle, cab or window state changes (stamp, sound.cpp:970-976).

**Default vehicle table** (DynObj.h:590-596). Columns as above.

| Row (placement) | rear cab | cab0 | front cab | window | ext. bound | free/other |
|---|---|---|---|---|---|---|
| internal | NONE | STRONG | NONE | SOME | STRONG | STRONG |
| engine | STRONG | NONE | STRONG | SOME | SOME | SOME |
| external | STRONG | STRONG | STRONG | SOME | SOME | NONE |
| external_ambient | VERYSTRONG | VERYSTRONG | VERYSTRONG | STRONG | STRONG | NONE |
| custom | NONE | NONE | NONE | NONE | NONE | NONE |

Reading the table:
- In your own cab, an external horn is × 0.447.
- Another loco's engine heard from your cab is × 0.806 (its engine row, column 5), then × 0.447 (your external row, cab column).
- In an external view bound to a vehicle, sounds of other vehicles are × 0.806 (column 4 of the bound vehicle's external row). That may be unintended (unsure).

**Overriding the vehicle table:**
- `internaldata: soundproofing:` followed by 30 floats, 5 rows × 6 columns in the row order above (DynObj.cpp:6935-6944). `-1` keeps the default; anything else becomes `sqrt(clamp(v,0,1))`.
- ET22 overrides only the custom row: `0.6 1.0 0.6 0.65 0.65 0.65`. Its compressor and converter use `placement: custom`.

**Per-sound `soundproofing:` (6 values)** replaces the vehicle row for that sound (step 2). Two caveats:
- **`-1` per sound is not handled.** The vehicle table treats -1 as "keep default", but a sound_source keeps -1 and uses it as the factor (sound.cpp:989-991). That sends a negative `AL_GAIN` and reference distance to OpenAL, which rejects them (AL_INVALID_VALUE), so the previous gain stays. Treat it as unsupported (unsure about the exact audible result).
- **Gauge-block `soundproofing:` is not square-rooted** (Gauge.cpp:264-275), unlike sound blocks and announcements. Those values act as raw gain factors.

### 2.8 Offsets and coordinate frame

`location() = vehicle.GetPosition() + VectorLeft·x + VectorUp·y + VectorFront·z` (sound.cpp:903-915).
- `GetPosition()` is the midpoint between the outer axles (DynObj.cpp:2643).
- The vehicle model matrix is `BasisChange(vLeft, vUp, vFront)` (DynObj.cpp:2672). So sound offsets and model/sub-model coordinates (cab controls) share one frame: **+x left, +y up, +z front**.
- In `locations:` and `wheel_clatter:` the z values are **negated** ("negative = ahead of centre"). The block `offset:` uses +z = front directly.
- Without an owner (scenery sounds), the offset is an absolute region position.

### 2.9 Global volume categories and settings

- **Categories** (sound.cpp:378, audiorenderer.cpp:164-170):
  - Sound with an owner vehicle (all vehicle and cab sounds): `vehicle`, scaled by `Global.VehicleVolume`.
  - No owner, range >= 0: `local`, scaled by `EnvironmentPositionalVolume`.
  - No owner, range < 0: `ambient`, scaled by `EnvironmentAmbientVolume`.
- **eu07.ini keys** (Globals.cpp:158-212, defaults in Globals.h:195-203):
  - `soundenabled`
  - `sound.volume` (listener gain, 0-2)
  - `sound.volume.vehicle`, `sound.volume.positional`, `sound.volume.ambient` (each 0-1)
  - `sound.volume.radio` (default 0.75)
  - `sound.maxsources` (`ALC_MONO_SOURCES` request, default 30)
  - `sound.asyncload` (decode on worker threads)
  - `sound.openal.renderer` (device name)
- **Pause.** The device is paused while dt == 0 (audiorenderer.cpp:383-391), and `RenderSounds` returns early while paused (DynObj.cpp:4362).

---

## 3. Finding the file on disk

`buffer_manager::create` (audio.cpp:245-287):
1. `name = ToLower(name)`, then the **extension is stripped** (last `.xxx`, unless the dot belongs to `..`; `utilities.cpp:525-541`). Backslashes were already turned into `/` by the parser.
2. **Vehicle folder.** If `Global.asCurrentDynamicPath` is set, look up `asCurrentDynamicPath + name`: first in the cache, then on disk.
   - It is set to the vehicle's `asBaseDir` (`"dynamic/" + BaseDir + "/"`, DynObj.cpp:1952) only while `TDynamicObject::LoadMMediaFile` runs (DynObj.cpp:5184 → cleared at 7054).
   - That window covers every `sounds:`, `locations:` and DynObj `internaldata:` key, plus the built-in defaults (6992-7041).
   - **It does not cover** `TTrain::LoadMMediaFile` / `InitializeCab` (Train.cpp:655-656, 10841-10842). Those run later with the path cleared, so cab sounds and cab control sounds skip the vehicle folder (unsure only in that no other place setting it was found; grep finds just these two).
3. **Explicit path.** If the name contains `/`, try it as given, relative to the game root.
4. **Shared folder.** Try `sounds/` + name (`paths::sounds`, utilities.h:39).
5. **Not found.** Log `Bad file: failed to locate audio file` and leave that part empty.

**Extension search order** at each location: `.ogg`, then `.flac`, then `.wav` (`find_file`, audio.cpp:389-397; `FileExists`, utilities.cpp:474-488). So `697_et22-motor0.wav` and `20786_et22_emergency_brake_1` both resolve to `sounds/697_et22-motor0.ogg` and `sounds/20786_et22_emergency_brake_1.ogg` in this pack. The pack has only `.ogg` files, plus one `engine.ogg` in a vehicle folder.

**Case.** The sound name is lowercased but `asBaseDir` is not. Lookup uses `std::filesystem::exists`, which is case-sensitive on Linux, so the files must be lowercase on disk (the pack's are).

**Cache key.** The path without extension (audio.cpp:369-373). The same sample referenced by several sounds or vehicles is decoded once. Cache keys include the folder, so a vehicle-folder hit is not reused for a `sounds/` lookup.

**Decoding** (audio.cpp:33-90):
- libsndfile (`sf_open` / `sf_readf_float`), so WAV, FLAC and OGG Vorbis (other formats depending on the libsndfile build).
- Every file is **mixed down to mono 16-bit** by averaging channels (with a log warning), at the file's own sample rate, into one `AL_FORMAT_MONO16` buffer.
- With `sound.asyncload`, decoding runs on (cores-1, clamped 1-8) worker threads; a buffer needed before it is ready is waited for (audio.cpp:112-210, 291-339).
- A caption file is loaded if present (2.1).

---

## 4. Vehicle sounds (DynObj.cpp)

### Notation

Values are fields of `TMoverParameters` (McZapkie/MOVER.h):
- `Vel` in km/h (1957), `Vmax` in km/h.
- `nrot`: wheel rev/s (1962).
- `enrot`: engine rev/s (2110).
- `nmax`: max motor rev/s (1750).
- `RventRot` and `RVentnmax`: rev/s; the .fiz value is in rpm and divided by 60 (Mover.cpp:11428-11429).
- `EnginePower` (1967), `eimv_pr`: fraction of available force (2195), `InverterFrequency` (1873), `UnitBrakeForce` (1974), `WheelFlat` (1964), `AccN` in m/s² (1960).
- `MotorBlowers[].revolutions`: rpm. `MotorBlowers[].speed`: a fraction of parent rpm when > 0, an absolute rpm when < 0 (1332-1336).
- `dizel_heat.rpmw` / `rpmw2`: fan rpm (1506-1509).

"ff/fo/af/ao" are the frequency/amplitude factor/offset. "Combined" means the sound is a table (2.3). **Legacy** gives the type and number mask from 1.1: S = single, M = multipart (begin main end), r = range, a = amp pair, f = freq pair.

### 4.1 `sounds:` block (DynObj.cpp:5922-6526)

`powertrain_sounds::render` returns immediately when `Power == 0` (DynObj.cpp:8096). Every "powertrain" row below is therefore silent on unpowered vehicles.

| Key | Member, placement, default range | Legacy | Load-time changes | Runtime: start/stop · pitch · gain (code) |
|---|---|---|---|---|
| `wheel_clatter:` | one `axle_sounds` per pair. external, range = first number | own syntax (1.1) | axles sorted by offset | See 4.4. |
| `engine:` (only if `Power > 0`) | `m_powertrainsounds.engine`, engine, 50 | S r a f | af /= `nmax·60 + Power·3`, except diesel and diesel-electric (divisor 1) (5957-5960) | Section 4.3. |
| `fakeengine:` | `fake_engine`, engine, 50 | S | — | While `Mains` is on: pitch `fo + clamp(eimv_pr,0,1)·ff`, gain `ao + p·af`. Exclusive looping (8276-8286). |
| `dieselinc:` | `engine_revving`, engine, 50 | S r | — | Diesel revving-up, 4.3. |
| `oilpump:` / `fuelpump:` / `waterpump:` / `waterheater:` | engine, 50 | S | — | While `OilPump/FuelPump/WaterPump/WaterHeater.is_active`: pitch `fo+ff`, gain `ao+af`, exclusive looping (8101-8140). |
| `tractionmotor:` (only if `Power > 0`) | `motors[]`, external, 50 | S r a f | af /= `nmax·60 + Power·3`. With `locations: tractionmotors:`, the template is copied to each motor (x,y from the template, z from the motor), each with a random `start()` in 0-1. Otherwise a single source (5993-6019). | 4.3. |
| `tractionacmotor:` (only if `Power > 0`) | `acmotors[]`, external, 50 | S r a f | per-motor copies as above; no af divisor | Only for `ElectricInductionMotor` with `InverterFrequency > 0.001`: pitch `fo + ff·InverterFrequency`, gain `ao + af·sqrt(|eimv_pr|)`, exclusive looping (8448-8466). |
| `motorblower:` | `motorblowers[]`, engine, 50 | S r a f | divisor D = `MotorBlowers[front].speed·nmax·60 + Power·3` if speed > 0; 1 if the sound has both begin and end; else `-speed`. af /= D, ff /= D. Per-location copies only when `motors` is non-empty (the code tests `motors.empty()`, not `motorblowers`) (6047-6076). | Each instance follows `MotorBlowers[offset.z > 0 ? front : rear]`. Plays while `revolutions > 1`. Combined: pitch = `revolutions·0.01` (table in rpm). Single: pitch `fo + ff·m`, gain `ao + af·m`, with m = 1 if speed < 0 and both bookends exist, else revolutions. Exclusive looping (8468-8488). |
| `inverter:` | `inverter`, engine, 50 | S r a f | — | Induction motor with `InverterFrequency > 0.001`: pitch `fo + ff·InverterFrequency`, gain `ao + af·sqrt(|eimv_pr|)` (8490-8503). |
| `ventilator:` | `rsWentylator`, engine, 50 | S r a f | af /= RVentnmax, ff /= RVentnmax | While `RventRot > 0.1`: pitch `fo + ff·RventRot`, gain `ao + af·RventRot`, so effectively × `RventRot/RVentnmax` (8505-8515). |
| `radiatorfan1:` | `radiator_fan`, engine, 50 | S | — | Diesel only, while `rpmw > 0.1`: pitch `0.5 + fo + ff·rpmw·0.01`, gain `0.5 + ao + 0.5·af·rpmw·0.01` (8517-8530). |
| `radiatorfan2` **(no colon in the code: `radiatorfan2:` is never matched, DynObj.cpp:6099)** | `radiator_fan_aux` | S | — | Same formula with `rpmw2` (8532-8542). |
| `transmission:` | `transmission`, engine, 50 | S r | af, ao, ff, fo preset to 0.029, 0.1, 0.005, 1.0 (a block can override) (6105-6116) | While `Vel > 0.1`: pitch `fo + ff·Vel`, gain `ao + af·Vel` (8590-8598). |
| `brakesound:` | `rsBrake`, external, **100** | S a f | ff /= 1+Vmax (6118-6124) | While `UnitBrakeForce > 10` and `Vel > 0.05`: `bfr = clamp(UnitBrakeForce / max(1, BrakeForceR(1,Vel) / (NAxles·max(1,NBpA))), 0, 1)`. Pitch `fo + Vel·ff`; gain `ao + sqrt(bfr · lerp(0.4, 1, Vel/(1+Vmax)))·af`. Exclusive looping (4634-4651). With the default ff=1 the table position is `100·Vel/(1+Vmax)`, i.e. **percent of Vmax** (capped at 99), not km/h. This is the ET22 case (the brake-15 … brake-130 thresholds); maybe unintended (unsure). |
| `brake:` (squeal) | `rsPisk`, external, 100 | S r a | if af > 10 then af=1, ao=0 (old threshold format); then ao ×= random 0.95–1.05 (6126-6137) | While `Vel > 2.5`: `v = ao + lerp(-1, 1, bfr)·af`; plays if v > 0.075. Pitch: combined `Vel·0.01` (table in km/h), else `fo+ff`. At `Vel <= 2.5` gain decays at `gain·2.5` per second. Stops when v < 0.05 (4660-4681). |
| `brakecylinderinc:` / `brakecylinderdec:` | external, 50 | S | — | `q = int(15 · BrakePress/MaxBrakePress[3])`. When q rises, the inc sound plays once (no flags, so instances overlap); when it falls, the dec sound. Pitch: combined `q·0.01` (table indexed by step 0-15), else `fo+ff` (4484-4513). |
| `epbrakeinc:` / `epbrakedec:` | external, 50 | S | — | Electro-pneumatic brake with `LocHandle` only: 50-step quantisation of the commanded EP pressure. Inc has a minimum interval of 0.05 s, dec 0.3 s. Combined pitch: inc = step·0.01, dec = (-change)·0.01 (4515-4556). |
| `emergencybrake:` | `m_emergencybrake`, **engine**, 50 | S | — | Starts when `EmergencyValveFlow > 0.025`, stops when < 0.015. Flow is smoothed, `fp = clamp(flow,0,1) + clamp(0.1·PipePress, 0, 0.5)`. Pitch `fo+ff`, gain `ao + clamp(fp,0,1)·af`. Exclusive looping (4558-4572). |
| `brakeacc:` | `sBrakeAcc`, external, 50 | S | sets `bBrakeAcc` | When the brake valve reports `sf_Acc` (accelerator): exclusive one-shot (4653-4658). |
| `unbrake:` (brake release hiss) | `rsUnbrake`, external, 50 | S r | af preset to **20** before parsing (6177-6182). Default sample `[1007]estluz` if missing (7008-7011). | `rate = lerp(rate, (lastBrakePress - BrakePress)/dt, 0.05)`. Gain `af · rate · (0.25 + 0.75·BrakePress/MaxBrakePress[3])`; ao is unused. Plays (exclusive looping) while rate > 0.05 and ratio > 0.05. Otherwise gain decays 0.5/s and the sound stops below 0.05 (4574-4595). |
| `springbrake:` / `springbrakeoff:` | external, 50 | S | — | On a `SpringBrake.Activate` change: activate or release plays as an exclusive one-shot, and the other is stopped (4683-4694). |
| `derail:` | `rsDerailment`, external, **400** | S r | — | When `EventFlag` is set and `DamageFlag` has `dtrain_out`: exclusive one-shot (5149-5157). |
| `curve:` | `rscurve`, external, **200** | S r | default sample `curve` if missing | `v = |AccN| · lerp(0.5, 1, clamp(Vel/40,0,1)) · (switch with R < 1500 ? 100 : 1)` when Vel > 5 and 1 < \|R\| < 15000, else 0. Plays if v > 0.05 with **gain = 2.5·v; af/ao are not used**. Pitch: combined `Vel·0.01` (km/h), else `fo+ff`. Exclusive looping (4971-4999). |
| `horn1:` / `horn2:` / `horn3:` | `sHorn1..3`, external, **1000** | M r | horn2 sets the AI's horn number when `iHornWarning` | Bits 1/2/4 of `WarningSignal`. When moving (Vel > 0.5) with the alarm chain pulled, `EmergencyBrakeWarningSignal` is ORed in. Exclusive looping while the bit is set, `stop()` (end part) when it clears (4826-4853). The cab horn keys set and clear the bits on press and release (Train.cpp:7844-7910). |
| `pantographup:` / `pantographdown:` | per pantograph, external, 50 | S | template copied to every pantograph (6225-6243). Offset `(0, vPos.y, -vPos.x)` of the pantograph (7072-7084). | **Up** plays when a pantograph's voltage goes from 0 to > 0, i.e. it touches a live wire. Front pantographs (offset z > 0) follow front voltage, rear ones rear voltage (3826-3835, 3869-3878). **Down** plays when `Pantographs[end].is_active` becomes false (3951-3983). Exclusive one-shots. |
| `compressor:` / `compressoridle:` | engine, 50 | M r | — | Only if `CompressorSpeed > 0`. While `CompressorFlag` is on: with no idle sound, the compressor loops. With an idle sound, idle loops while `CompressorGovernorLock` is set and the compressor otherwise. If `CompressorPower == 3` (engine driven), pitch = `clamp(EngineMaxRPM/EngineIdleRPM · EngineRPMRatio, 0.5, 2.5)` (4413-4447). |
| `converter:` | `sConverter`, engine, 50 | M r | — | While `ConverterFlag` is on, exclusive looping. Pitch for `ElectricSeriesMotor` with voltage > 0: `voltage/(NominalVoltage·RList[RlistSize].Mn) · (ff + fo)`. Otherwise `fo + ff`. Clamped 0.75–1.25 (4377-4396). |
| `brakingresistorventilator:` | `sBRVent`, engine, 50 | M r | — | While `BRVentilators` is on, exclusive looping (4400-4407). |
| `heater:` | `sHeater`, engine, 50 | S | — | While `Heating` is on and (not diesel-powered or \|enrot\| > 0.01). Pitch: combined `|enrot|·60·0.01` (table in engine rpm), else 1. Exclusive looping (4457-4472). |
| `battery:` | `m_batterysound`, engine, 50 | S | — | While `Battery` is on, exclusive looping (4474-4480). |
| `turbo:` | `engine_turbo`, engine, 50 | M r | gain(0) | 4.3. |
| `small-compressor:` | `sSmallCompressor`, engine, 50 | M r | — | While `PantCompFlag` (pantograph compressor) is on, exclusive looping (4449-4455). |
| `departuresignal:` | one per door speaker, **general, 25** | M r | copied to each speaker from `locations: doors:` at `(0, 3, -z)` (6295-6304, 6572-6575) | While `Doors.has_warning`, `DepartureSignal` and 24 V/110 V power are on: exclusive looping (4710-4731). Without `doors:` nothing plays. |
| `dooropen:` / `doorclose:` / `doorlock:` / `doorunlock:` / `doorstepopen:` / `doorstepclose:` / `doorpermit:` | per door, **general**, 50 | S | template copied to each door entry; per-door offsets kept (6306-6388) | Per side (`Doors.instances[left/right]`):<br>• **open** while `is_opening`, with no step unfolding or `step_type == 2`, and position < 0.5.<br>• **close** while `is_closing` and position > 0.<br>• **step open** while `step_unfolding`; **step close** while `step_folding` and step_position < 1.<br>All are exclusive, and each stops its counterpart (4757-4810).<br>• **lock/unlock** on a `Doors.is_locked` change, all doors (4811-4824).<br>• **permit** on the `doorpermit` flag, all doors (5022-5030; set by `PermitDoors_`, Mover.cpp:8541). |
| `unloading:` / `loading:` | `m_exchangesounds`, general, range **-L/2** (non-positional within half the length; ×0.4 because range < -1) | S | — | While `LoadStatus & 1` (unloading) or `& 2` (loading): exclusive, re-triggered every frame, so effectively repeated. Stopped when the bit clears (4696-4709). |
| `sand:` | `sSand`, external, 50 | M r | — | While `SandDose` is on, exclusive looping (4626-4632). |
| `releaser:` | `sReleaser`, external, 50 | M r | — | While `Hamulec->Releaser()` is on: gain `clamp(BrakePress·1.25, 0, 1)`, exclusive looping (4597-4608). |
| `outernoise:` | `m_bogiesounds[]` (`EU07_SOUND_BOGIESOUNDS` is defined, DynObj.h:27), external, **200** | S a f (Chunkrange = Vmax) | af /= 1+Vmax, ff /= 1+Vmax. Copied per `locations: bogies:` entry (z per bogie), else a single source. Random `start()`: odd entries 0–0.3, even 0.5–0.8 (6413-6451). | 4.5. |
| `wheelflat:` | `m_wheelflat`, external, 200 | S f | default `lomotpodkucia 0.23 0.0` (ff=0.23, fo=0) | Rail vehicles only, while Vel > 1 and `WheelFlat > 5`: pitch `fo + |nrot|·ff`, gain `ao + af·(1 + Vel/Vmax + clamp(WheelFlat/60,0,1))/3`. Exclusive looping (4956-4969). |
| `announcements:` | `m_pasystem.announcements[]`, engine; range set at play to **-L/2** | block (1.3) | — | `announce()` (7893-7950) queues chime (optional), then the announcement, then the station-name sound from the timetable. The queue plays one item at a time while 24 V/110 V power is on, and is cleared when power is lost (4732-4756). Needs door speakers (`doors:`). |

### 4.2 `internaldata:` keys read by DynObj (DynObj.cpp:6622-6978)

| Key | Member, placement, range | Legacy | Runtime |
|---|---|---|---|
| `ignition:` / `shutdown:` | `engine_ignition` / `engine_shutdown`, engine, 50 | S | On a `Mains` change: on → stop shutdown, play ignition (exclusive). Off → stop ignition, play shutdown. Pitch `fo+ff`, gain `ao+af` (8142-8172). |
| `linebreakerclose:` / `linebreakeropen:` | engine, 50 | S | Same `Mains` edge: close on → on, open on → off. One-shot without flags; pitch `fo+ff`, gain `ao+af` (8153-8169). |
| `engageslippery:` | `rsEngageSlippery`, engine | S a f | ff /= 1+nmax. While `dizel_engage > 0.1`: if \|`dizel_engagedeltaomega`\| > 0.2, pitch `fo + ff·|Δω|`, gain `ao + af·dizel_engage`. Otherwise pitch 1, gain `ao + 0.2·af·enrot/nmax` when engage > 0.2, else 0. Exclusive looping (8321-8338). |
| `retarder:` | `retarder`, engine | S a f | If `hydro_R`: pitch `fo + ff·|hydro_R_n|`, gain `ao + af·hydro_R_Fill`. Plays while gain > 0.01, speed > 1 and the clutch is active (8340-8353). |
| `wiperfrompark:` / `wipertopark:` | internal, 50 | S | Exclusive one-shot when the wiper leaves park, or starts returning. Re-armed at the end stop or in park (4108-4143). |
| `relay:` | `motor_relay`, engine | S | Mover `SoundFlag` relay bit (set on each automatic-starter step `AutoRelayCheck`, fuse trip, relay reset, compressor over-pressure vent: Mover.cpp:1585, 4386, 6561, 6624, 6988). If neither shuntfield nor parallel is set: one-shot. Pitch: combined `MainCtrlActualPos·0.01` (table indexed by resistor notch), else `fo+ff`. Gain `ao + (loud ? 1.0 : 0.8)·af` (8545-8588). |
| `shuntfield:` | `motor_shuntfield`, engine | S | Relay with the shuntfield bit: one-shot. Combined pitch `ScndCtrlActualPos·0.01` (indexed by field-shunt step). Gain as for relay. |
| `wejscie_na_bezoporow:` / `wejscie_na_drugi_uklad:` | `dsbWejscie_na_bezoporow` / `motor_parallel`, engine | S | Relay with the parallel bit: `loud` plays the first (resistor-less notch), otherwise the second (series→parallel). One-shot. Defaults `wejscie_na_bezoporow` / `wescie_na_drugi_uklad` when `Power > 0` (6996-7006). |
| `directionrelay_d:` / `_r:` / `_n:` | `sDirectionRelayD/R/N`, engine | S | Triggered from **Train.cpp**, player train only. On a `DirActive` change with 24 V: play D (>0), N (0) or R (<0) once. On a battery change: `SetupDirectionRelays` (Train.cpp:9628-9665). |
| `pneumaticrelay:` | `dsbPneumaticRelay`, external | S | On `SoundFlag & sound::pneumatic`, gain 1.0 if loud else 0.8 (5013-5021). **Nothing in Mover sets `sound::pneumatic` at this commit** (grep), so it never plays. |
| `slipperysound:` | `rsSlippery`, external, **100** | S a | af /= 1+Vmax. While `SlippingWheels`, `UnitBrakeForce > 100` and Vel > 1 (wheels sliding under braking): gain `ao + af·Vel/Vmax`, exclusive looping. Stopped when not slipping (4610-4624). af is in effect divided by (1+Vmax) and then multiplied by Vel/Vmax. |
| `couplerattach:` / `brakehoseattach:` / `mainhoseattach:` / `controlattach:` / `gangwayattach:` / `heatingattach:` and the matching `...detach:` | `m_couplersounds[front,rear]`, external, 50 | S | Template copied to both couplers. Offsets `(0, 1, ±L/2)` (7092-7129). Triggered from the coupler's `sounds` bitmask, set by Mover on coupling changes (Mover.cpp:625-640, 688-698). With the `detach` bit, the detach variants play; otherwise attach. One-shots (5092-5134). Defaults `couplerattach_default` / `couplerdetach_default` (7013-7030). (Pack note: 503 vehicles spell it `gengwaydetach:`, which is not recognised.) |
| `couplerstretch:` / `couplerstretch_loud:` | external | S | Coupler slack runs out at Vel > 1 and \|dV\| > 0.005; loud if \|dV\| > 0.035 (Mover.cpp:5298-5302). Plays the loud variant if defined, else the normal one; gain 1, exclusive. Default `couplerstretch_default`. |
| `bufferclamp:` / `bufferclamp_loud:` | external | S | Buffers close in with dV < -0.1; loud if \|dV\| > 5 (Mover.cpp:5292-5297). Same selection. Default `bufferclamp_default`. |
| `coupleradapterattach:` / `coupleradapterremove:` | external | S | Adapter bits, one-shot (5135-5140). |
| `startjolt:` | `m_startjolt`, general | S | Once per start: when accelerating (`AccSVBased > 0.1` in the movement direction) at 1–15 km/h. Re-armed when stopped (4364-4369, 5001-5010). |
| `soundproofing:` | vehicle table | 30 floats | 2.7. |

### 4.3 Engine, traction motors, revving, turbo (DynObj.cpp:8173-8453)

**Engine** (while `Mains` is on and \|enrot\| > 0.01, or the engine type is `Dumb`):
- Pitch `f = fo + ff·|enrot|·n`, where n = 0.6 for a combined sound (table in **engine rpm**) and 1 otherwise (rev/s). For `Dumb`: f -= `0.2·EnginePower/(1+Power·1000)`.
- Target volume:
  - Diesel-electric: `ao + af·(0.25·EnginePower/Power + 0.75·EngineRPMRatio)`.
  - Diesel (enrot > 0): `ao + af·dizel_fill` when EnginePower > 0, else `ao + af·|enrot/dizel_nmax|`.
  - Others: `ao + af·(EnginePower + |enrot|·60)`.
- Smoothing: `engine_volume = lerp(engine_volume, target, 0.25)` every frame. The engine plays (exclusive looping) while it is >= 0.05 and stops below.

**Revving (`dieselinc:`, diesel only):**
- `rc` decays 2.5/s. It grows 5/s while rpm > 1.01·idle and rising faster than 1 rpm/s; clamp 0–1.25.
- Revving gain is `0.8·rc`; it plays (exclusive, re-triggered) above 0.02.
- Pitch is the engine's `f` if the revving sound is combined, else `fo+ff`.
- If the revving sound is combined, the engine gain becomes `max(0, engine_volume - revvol)`.

**Turbo** (`TurboTest > 0`):
- `goalpitch = max(0.025, (pd + fo)·ff)`, where pd = `enrot/dizel_nmax·dizel_fill` for diesels, else 1.
- `goalvol = (MainCtrlPos >= TurboTest and enrot > 0.1) ? max(0, (tp + ao)·af) : 0`.
- `tp` slews towards goalpitch at +0.4/s up and -0.2/s down. Volume slews at ±0.4/s.
- Pitch sent is `0.4 + 0.4·tp`. Plays while volume > 0.05 (8293-8319).

**Traction motors (`motors[]`):** while \|nrot\| > 0.01.
- `mr = |nrot|·Transmision.Ratio` (motor rev/s).
- Pitch `fo + ff·mr·n`, with n = 0.6 for a combined sound (table in **motor rpm × ff**). ET22: ff = 0.04, so table point = 0.04·rpm.
- Volume:
  - Induction motor: `ao + af·(EnginePower + 2·mr)`.
  - Series motor: `ao + af·(EnginePower + 60·mr)`. Below volume 1.0 and power 100 it adds a small random term. It adds +0.8 in dynamic braking with EnginePower > 0.1.
  - Others: `ao + af·60·mr`.
- Then volume ×= `max(0.25, momentum)`, where `momentum = clamp(m - dt + |Mm|/60·dt, 0, 1.25)`.
- `motor_volume = lerp(motor_volume, v, 0.25)`. All motor instances get the same pitch and gain and play (exclusive looping) while it is >= 0.05.

### 4.4 Wheel clatter (DynObj.cpp:3622-3699, init 2445-2455)

- **Rail length.** It is the track's `fSoundDistance`, the 4th number of the track header; **switches force 10 m** (Track.cpp:439-447). With -1 there is no clatter.
- **Axle phase.** Each axle keeps a phase `distance` modulo the rail length:
  - Initialised from the axle position when the vehicle is placed.
  - Reset to the axle offset when the rail length changes.
  - Advanced by the vehicle movement × direction-in-consist.
- **Trigger.** Each wrap triggers a non-exclusive one-shot (overlap allowed) when Vel > 0.1.
- **Gain:** `lerp(0.8, 1.2, clamp(iQualityFlag/10, 0, 1.5))`, × 1.3 in a tunnel, × 1.5 on a bridge.
- **Pitch:** combined `Vel·0.01` (table in km/h), else 1.0.
- af/ao/ff/fo are ignored.

### 4.5 Running noise outside (`outernoise:`, DynObj.cpp:4854-4955)

**When it plays:** Vel > 0.5, bogie sounds exist, and the vehicle is **not part of the player's consist while the player is in the cab**. In other words it plays in free-fly, for standalone vehicles, for other consists, or with the cab window open.
- Pitch `fo + ff·Vel·n`, n = Vmax·0.01 for combined. With ff pre-divided by 1+Vmax, the table point is about Vel in km/h.
- Gain:
  - `(ao + af·Vel)`, × `(1 + 0.125·bfr)` when braking.
  - × `lerp(0.8, 1.2, clamp(iQualityFlag/20, 0, 1))`.
  - Single sample only: × `clamp(Vel/40, 0, 1)`.
  - × 1.3 in a tunnel, × 1.5 on a bridge.
- Plays (exclusive looping) if gain > 0.05, else stops.
- When it should not play: `stop(Skipend = !FreeFly)`, so no end part when switching into the cab.

### 4.6 Default positions (DynObj.cpp:7056-7129)

These apply only to sounds still at offset (0,0,0) after loading.
- **All powertrain sounds** (inverter, relays, ventilator, engine, ignition/shutdown, revving, turbo, pumps, fans, transmission, engage, retarder), plus converter, BR ventilator, compressors, heater and battery: `(0, H/2, 0)` (8077-8091, 7058-7069).
- **Pantographs:** `(0, vPos.y, -vPos.x)`.
- **Couplers:** `(0, 1, ±L/2)`.
- **Doors** (from `locations: doors: <z> left|right|both ... end`): left `(W/2, H/2, -z)`, right `(-W/2, **2.0**, -z)` (DynObj.cpp:6547, 6561; the left/right heights differ).
- **Door speakers:** `(0, 3, -z)`.
- **Motors, AC motors, blowers** (`tractionmotors:`) and **bogie noise** (`bogies:`): `(0, 0, -z)`.
- **Everything else** (brakes, horns, sand, curve, clatter except z, derail and others): the vehicle origin, unless `offset:` is given.

Built-in default samples (DynObj.cpp:6992-7041) are assigned **only to rail vehicles** (`CategoryFlag & 1`), and only when the key is absent:
- `wejscie_na_bezoporow` and `wescie_na_drugi_uklad`, only if `Power > 0`.
- `[1007]estluz` for `unbrake`.
- `couplerattach_default`, `couplerdetach_default`, `couplerstretch_default`, `bufferclamp_default`.
- `lomotpodkucia 0.23 0.0` for `wheelflat`.
- `curve`.

These defaults are looked up while the vehicle folder path is still set, so the vehicle folder is searched first, then `sounds/`.

### 4.7 Event-flag plumbing

Mover sets `SoundFlag` and `Couplers[i].sounds` bits (MOVER.h:355-392). `RenderSounds` consumes and clears them every frame (5144-5146). `RenderSounds` is only called for vehicles on tracks in scene cells within 2750 m of the camera (section 6).

---

## 5. Cab sounds (Train.cpp)

They exist only for the **player's train**. In single player, every other `TTrain` is purged (`train_table::update`, Train.cpp:12336-12357). Their owner is the occupied vehicle, so they use vehicle volume and that vehicle's soundproofing table. All are `std::optional`, and absent means silent.

### 5.1 Where they are defined

- **Keys:** the same `.mmd` `internaldata:` block (`TTrain::LoadMMediaFile`, Train.cpp:10199-10324).
- **Lookup:** sample lookup skips the vehicle folder (section 3).
- **Reloading:** loaded again on every cab or vehicle change (Train.cpp:655-656, 10184, 10841-10842).
- **Cab section** (`InitializeCab`, Train.cpp:10326-10670):
  - Parsing starts at `cab<N>definition:` (N = 1 front, 2 rear, 0 for CabOccupied 0) and runs to the end of the file or `cab0definition:`.
  - The fixed sequence first: `cab<N>definition: x1 y1 z1 x2 y2 z2` (cab box), then optional `driver<N>angle:`, `driver<N>pos:` and `driver<N>sitpos:`.
  - Then any number of `cab<N>model:`, control (gauge) and lamp (button) entries.
  - Controls after the cab definitions are shared by both cabs. In the ET22 they follow `cab2model:`, and each cab resolves the sub-model names in its own cab model.

### 5.2 Cab sound keys (table at Train.cpp:10204-10230)

All are placement `internal`. **R** is the range; `7.5` = `EU07_SOUND_CABCONTROLSCUTOFFRANGE`, and `-1` = global, at the listener. All are type single.

| Key | Member | R | Legacy | Trigger (Train.cpp) |
|---|---|---|---|---|
| `ctrl:` | `dsbNastawnikJazdy` | 7.5 | — | Fallback click for the master controller and joint controller gauges (9258-9292) (5.4). |
| `ctrlscnd:` | `dsbNastawnikBocz` | 7.5 | — | Fallback for the secondary controller / field shunting (9309). |
| `reverserkey:` | `dsbReverserKey` | 7.5 | — | Fallback for the reverser (`dirkey:`) (9333-9337). |
| `switch:` | `dsbSwitch` | 7.5 | — | Generic fallback for about 230 switch/button gauges (`UpdateValue(v, dsbSwitch)`). |
| `pneumaticswitch:` | `dsbPneumaticSwitch` | 7.5 | — | Fallback for the anti-slip button; one-shot on EP-fuse and EZT brake-handle transitions (1004-1006, 2234-2385). |
| `buzzer:` | `dsbBuzzer` | 7.5 | — | Alerter (CA/SHP vigilance) `SecuritySystem.is_beeping()` with 24 V/110 V power: pitch `fo+ff`, gain `ao+af`, `play(looping)` if not already playing. Stopped when the condition ends or power is lost (9981-10004). |
| `buzzershp:` | `dsbBuzzerShp` | 7.5 | — | Cab-signal beeping `is_cabsignal_beeping()`, same pattern (9963-9979). |
| `radiostop:` | `m_radiostop` | 7.5 | — | Radio on, powered, and `RadioStopFlag` set: exclusive looping (10116-10128). |
| `slipalarm:` | `dsbSlipAlarm` | 7.5 | — | `mvControlled->SlippingWheels` and Vel > 1: exclusive looping (9859-9870). |
| `distancecounter:` | `m_distancecounterclear` | 7.5 | — | After the train travelled its own length since the meter was armed: exclusive one-shot; pitch `fo+ff`, gain `ao+af` (10005-10018). |
| `tachoclock:` | `dsbHasler` | 7.5 | — | Speed-recorder ticking. `fTachoCount` rises 3/s (max 3) while tacho speed > 1 and falls 0.66/s otherwise. Starts (exclusive looping) at count >= 3, stops below 1. Pitch: combined `fTachoVelocity·0.01` (table in km/h shown), else `fo+ff`; gain `ao+af` (8478-8487, 9944-9957). Positioned on the `tachometer:` gauge if present (11963-11966). |
| `airsound:` | `rsHiss` | 7.5 | a | Brake valve exhaust. FV4a/FVel6: `p = lerp(p, Handle->GetSound(s_fv4a_b), 0.05)`, `vol = 0.5·(af·p·0.25 + ao)` when p > 0. Other valves: `p = (4p + max(0, dpMainValve))/5`, `vol = 2·af·p + ao`. Exclusive looping above 0.05 (9720-9814). |
| `airsound2:` | `rsHissU` | 7.5 | a | Main-pipe filling. FV4a: `s_fv4a_u`, smoothing 0.25, `vol = 0.5·(af·p + ao)`. Others: negative `dpMainValve`, `vol = -af·p + ao`, threshold 0.01. |
| `airsound3:` / `airsound4:` / `airsound5:` | `rsHissE` / `rsHissX` / `rsHissT` | 7.5 | a | FV4a/FVel6 only: `vol = 0.5·(GetSound(s_fv4a_e / _x / _t)·af + ao)`. These are the emergency, control-chamber and timing-reservoir exhausts (9754-9795). |
| `localbrakesound:` | `rsSBHiss` | 7.5 | a | Local (independent) brake release. `rate = lerp(rate, 10·ΔLocBrakePress/dt, 0.1)`. While rate < -0.05 and LocBrakePress > BrakePress - 0.05: gain `clamp(ao + af·(-rate)·0.05, 0, 1.5)`, exclusive looping. Otherwise decays 0.1/s and stops below 0.05 (9673-9699). Falls back to a copy of `airsound:` if missing. |
| `localbrakesound2:` | `rsSBHissU` | 7.5 | a | Local brake apply, rate > 0.05, same formula (9700-9718). Falls back to `airsound2:`. |
| `brakesound:` | `rsBrake` (cab copy) | -1 | a f | ff /= 1+Vmax. Same formula as the vehicle `brakesound`, but **in free-fly the gain is set to 0 instead of stopping** (keeps the begin part from replaying) (9832-9850). |
| `fadesound:` | `rsFadeSound` | 7.5 | — | Always plays, exclusive looping (cab ambience such as a clock) (9852-9857). |
| `runningnoise:` | `rsRunningNoise` | -1 | a f (Chunkrange = Vmax) | ff /= 1+Vmax. Only in the cab (not free-fly, window closed) with Vel > 0.5. Pitch `fo + ff·Vel·n`, n = Vmax·0.01 if combined (table ≈ km/h), else 1. Gain `ao + af·lerp(Vel/(1+Vmax), 1, 0.5)`, × `(1 + 0.125·bfr)` when wheels turn, × `lerp(0.8, 1.2, clamp(quality/20, 0, 1))`, × `clamp(Vel/25, 0, 1)` for a single sample. Plays above 0.05. Leaving: `stop(Skipend = FreeFly)` (9893-9906, 10064-10097). |
| `resonancenoise:` / `windsound:` | `rsResonanceNoise` / `rsWindSound` | -1 | a f (Vmax) | ff /= 1+Vmax. Same conditions as running noise. Pitch `fo + ff·Vel·Vmax·0.01` (**always** with the Vmax·0.01 normaliser, even for a single sample). Gain `ao + af·lerp(Vel/(1+Vmax), 1, 0.5)`, no other factors (9872-9891, 10045-10062). |
| `huntingnoise:` | `rsHuntingNoise` | -1 | a f (Vmax) | Running-noise formula while `IsHunting`, then gain × `clamp((Vel - fadein_begin)/(fadein_end - fadein_begin), 0, 1)` from `huntingspring:` (9907-9925). |
| `rainsound:` | `m_rainsound` | -1 | — | In the cab (not free-fly, window closed) when `Global.Weather == "rain:"`. Combined: pitch `Overcast - 1` (table 0-99 = rain intensity). Gain `ao + af`. Exclusive looping. Default sample `rainsound_default` (9926-9942, 10273-10279). |
| (no key) radio messages | `m_radiosound` template, internal, range 15 | — | — | Scenario radio messages are copied into this template. They are skipped if the receiver is outside the message source's range. Gain = `m_radiovolume` when the radio is on, powered, and on the message's channel, else 0. Positioned on the `i-radio:` lamp, else the cab centre (10099-10133, 10884-10898, 10601-10610). |

### 5.3 Cab sound positions (Train.cpp:10332-10351, 10600-10654)

On every `InitializeCab`, offsets are reset to the values from the `.mmd` (`CabSoundLocations`). Sounds still at (0,0,0) are then placed:
- `reverserkey` → the reverser gauge.
- `ctrl` → the joint controller, else the master controller.
- `ctrlscnd` → the secondary controller.
- All `airsound*` → the brake controller.
- `localbrakesound*` → the local brake handle, else the brake controller.
- `buzzer`, `buzzershp` and `distancecounter` → the `i-security_aware` lamp (`btLampkaCzuwaka`).
- `radiostop` → the radio position.
- `switch`, `pneumaticswitch`, `fadesound`, the noises, `tachoclock` (unless a tachometer exists), `slipalarm` and `rainsound` → the **cab centre**: `(CabPos1 + CabPos2)/2 + (0, 1, 0)`.

A gauge position is its sub-model's offset from the model root (`TSubModel::offset`, Model3d.cpp:1658-1686). When the matrix is identity, it falls back to the average of the vertices. The frame is the vehicle frame (2.8).

### 5.4 Sounds on cab controls and lamps

**Gauges (`TGauge`, Gauge.cpp)**, i.e. levers, switches and buttons:
- **Old form:** `<key>: <submodel> <type> <scale> <offset> <friction> [endvalue endscale]`, with no sounds.
- **Block form:** `<key>: { <submodel> <rot|rotvar|mov|movvar|wip|dgt> <scale> <offset> <friction> [endvalue endscale] <key: value>... }` (Gauge.cpp:98-176). Map keys (Gauge.cpp:236-288):

| Key | Meaning |
|---|---|
| `soundinc:` / `sounddec:` | Sound played when the control's target value increases or decreases, if no value-specific sound matched. |
| `sound<N>:` (N int, may be negative) | One-shot sound for **reaching value N exactly**. This is a value → sample map (`std::map<int, sound_source>`), **not** a crossfade table. |
| `soundon:` / `soundoff:` | Sound on the state input switching on or off (lamp-style gauges with a bool state, e.g. tempomat or door permit buttons). Played in `Update()` (358-382). Unused in the pack. |
| `soundproofing:` | 6 factors applied to every sound of this control that has no own table. **Not square-rooted** (raw factors). |
| `type:` | `push` / `impulse` / `return` → push; `delayed`; `pushtoggle`; anything else → toggle. This affects control behaviour, not sound. |

All gauge sounds are `sound_source(internal, 7.5)` (Gauge.h:109-114). Each value is a single sample: a file, `[random set]` (ET22 `universal1`, `radiochannel_sw`) or a `{}` block. Owner is the vehicle; positioned at the control's sub-model unless an offset was given (Gauge.cpp:75-95).

**How a control plays** (`TGauge::UpdateValue`, Gauge.cpp:292-340). TTrain calls `ggX.UpdateValue(newvalue[, fallback])` every frame with the simulation's current value. Nothing happens unless `round(100·value)` differs from the same rounding of the previous target. Then:
1. If the new value is a whole number N and `sound<N>` exists, play it (one-shot) and stop.
2. A non-whole value switches the control to **exclusive** mode for good (`m_soundtype`), meaning continuous handles do not stack clicks.
3. If the value rose and `soundinc` exists, play it (exclusive if in that mode). If it fell and `sounddec` exists, play that.
4. Otherwise, if TTrain passed a fallback (`ctrl:`, `ctrlscnd:`, `reverserkey:`, `switch:` or `pneumaticswitch:`), play the fallback with the same flags.

So the sounds follow **state changes of the simulated device**, not mouse clicks. A key press, a mouse drag or an AI action that changes the value all click the same way. Examples from the ET22:
- `mainctrl: { nastawnik rot ... sound0: ... sound48: ... }` gives one sample per notch.
- `brakectrl: { ... sound-2: ... sound6: ... }` gives one sample per brake handle position. The handle's `fBrakeCtrlPos` is fractional between notches, which also switches it to exclusive mode.
- `dirkey: { ... soundinc: ... sounddec: ... sound0: ... }` plays a dedicated sample for neutral and inc/dec otherwise.

**Lamps (`TButton`, Button.cpp):**
- Old form: `i-xxx: <submodel>`.
- Block form: `i-xxx: { <submodel> soundinc: s sounddec: s }` (Button.cpp:40-97).
- On a lamp state change (`Turn` or `Update`), on plays `soundinc` and off plays `sounddec` (113-156). Placement internal, range 7.5 (Button.h:56-57), positioned on the lamp's sub-model.
- `btLampkaRadioMessage.gain(m_radiovolume)` reuses this for the radio lamp.

---

## 6. Cab versus outside, and limits

**Listener in the cab vs outside:**
- **Soundproofing** depends on the listener column (2.7): own cab, other cab, window, bound external camera, free camera or another vehicle. The listener's own vehicle adds its external-row factor to everything coming from other vehicles.
- **Outside running noise** (`outernoise`) of the player's own consist is **muted while in the cab with the window closed**. It is replaced by the cab `runningnoise` / `resonancenoise` / `windsound` / `huntingnoise` / `rainsound`, which are global-range sounds at the listener and run only in the cab with the window closed (DynObj.cpp:4859-4865, Train.cpp:9872-9942). When the view switches, the cab noises stop without their end part (Skipend = FreeFly) and the outside noise does the reverse (DynObj.cpp:4946-4954).
- **Cab `brakesound`** is muted (gain 0) in free-fly, not stopped. The external `brakesound` keeps playing.
- **Cab sounds exist only** for the player's train, in single player. Cab control sounds use range 7.5 m: start culling at 37.5 m, stop at 56 m.
- **Camera jumps** zero the listener velocity, so there is no doppler spike (audiorenderer.cpp:446-464).

**Limits and culling:**
- **Vehicles updated:** `RenderSounds` runs only for vehicles on tracks in scene cells within **2750 m** of the camera (`basic_region::update_sounds`, scene.cpp:1140-1146 → cell test 800-808 → `TTrack::RenderDynSounds`, Track.cpp:1785-1789). Their sound-event flags are only consumed then.
- **Per source:** start rejected beyond `min(2750, 5·|range|)`, stopped beyond `min(3000, 7.5·|range|)` (3000 for begin+main queues), faded to silence between `range` and `1.25·range`.
- **No per-vehicle cap.** One emitter can have several live OpenAL sources: overlapping one-shots, two crossfading table entries, plus begin/end parts.
- **Global cap:** OpenAL mono sources are requested from `sound.maxsources` (default 30; `ALC_MONO_SOURCES`, audiorenderer.cpp:607). When `alGenSources` fails, the renderer steals the playing **single-buffer** source with the lowest weight `range² / (distance² + 1)`, but only if that weight is < 1, i.e. the stolen sound is outside its nominal range. Otherwise the new sound gets no source and is dropped (audiorenderer.cpp:502-568).
- **Source reuse.** Finished sources go back to a spare stack (audiorenderer.cpp:470-488).

---

## 7. Quirks found in the code

1. **`radiatorfan2`** is matched without a colon (DynObj.cpp:6099), so `radiatorfan2:` is ignored.
2. **`motorblower:` per-location copy** tests `motors.empty()` instead of `motorblowers.empty()` (DynObj.cpp:6060).
3. **`sound::pneumatic`** is never set by Mover, so `pneumaticrelay:` is effectively dead.
4. **Per-sound `soundproofing: -1`** reaches OpenAL as a negative gain (2.7). Only the vehicle table treats -1 as "default".
5. **Gauge `soundproofing:`** is not square-rooted; sound and announcement blocks are.
6. **Vehicle `brakesound`** table position is percent of Vmax, not km/h (4.1). `curve:` ignores af/ao. `slipperysound` divides by Vmax twice.
7. **Right-side doors** are placed at height 2.0 m, left-side at H/2 (DynObj.cpp:6547 vs 6561).
8. **Cab sounds and cab control sounds** are not searched in the vehicle folder (section 3).
9. **Pack typos** the parser does not recognise:
   - `gengwaydetach:` (503 files).
   - `placement: externa` (1) and `placement: interal` (8), which fall back to the default placement.
   - 3 `outernoise:` entries in `internaldata:` that nothing reads.

---

# Part B: the pack

Measured 2026-11-10 by scripts that read as MaSzyna reads (`cParser`: its
separators, comments, BOM, `include file params end` with `(pN)`, random
`include [ a b ] end`; `DynObj::LoadMMediaFile`, `TTrain::LoadMMediaFile`,
`TTrain::InitializeCab`, `TGauge::Load`, `TButton::Load`,
`sound_source::deserialize`). Every alternative of a random include set was
read. "Sites" are distinct (file, line, key): text shared through includes
counts once.

## B.1 Counts

| | count |
|---|---:|
| `.mmd` files | 1,461 in 229 vehicle folders (pkp 1,306, cd 78, road 59, others 18) |
| only an include wrapper (`include x.mmd.inc ... end`) | 777 |
| with `sounds:` / `internaldata:`, includes expanded | 1,440 / 1,355 |
| with a cab (`cab<N>definition:`) | 1,122 |
| include files reached | 476, none missing, no case mismatch |
| random include sets | 468 sets in 436 `.mmd` (2 to 13 alternatives) |
| distinct sound definitions | 73,122 |
| recognised keys per `.mmd`, median / max | `sounds:` 17 / 33, `internaldata:` 17 / 37 |

## B.2 Keys

`sounds:` (DynObj), by `.mmd` using them; "legacy" is the one-line form, its
numbers as in A.1.1 (R range, A amplitude pair, F frequency pair, multi =
begin main end):

| key | .mmd | sites | block | legacy | legacy numbers |
|---|---:|---:|---:|---:|---|
| `wheel_clatter:` | 1,427 | 706 | – | 706 | own syntax |
| `brake:` | 1,397 | 695 | 600 | 95 | R A |
| `outernoise:` | 1,359 | 699 | 699 | 0 | A F |
| `curve:` | 1,351 | 693 | 682 | 11 | R |
| `unbrake:` | 1,306 | 673 | 512 | 161 | R |
| `releaser:` | 1,279 | 626 | 0 | 626 | multi R |
| `brakesound:` | 1,096 | 441 | 441 | 0 | A F |
| `derail:` | 915 | 469 | 0 | 469 | R |
| `sand:` | 844 | 404 | 43 | 361 | multi R |
| `horn1:` / `horn2:` / `horn3:` | 741 / 699 / 204 | 296 / 277 / 46 | most | 92 | multi R |
| `emergencybrake:` | 707 | 364 | 364 | 0 | – |
| `dooropen:` / `doorclose:` | 679 | 336 | 217 | 119 | – |
| `loading:` / `unloading:` | 667 | 317 | 105 | 212 | – |
| `tractionmotor:` | 551 | 317 | 268 | 49 | R A F |
| `brakeacc:` | 540 | 280 | 242 | 38 | – |
| `compressor:` | 524 | 268 | 239 | 29 | multi R |
| `brakecylinderinc:` / `dec:` | 513 / 41 | 192 / 29 | all | 0 | – |
| `converter:` | 496 | 286 | 264 | 22 | multi R |
| `pantographup:` / `down:` | 401 | 232 | 108 / 97 | 124 / 135 | – |
| `small-compressor:` | 387 | 221 | 123 | 98 | multi R |
| `engine:` | 329 | 157 | 119 | 38 | R A F |
| `departuresignal:`, `doorlock:`/`doorunlock:` | 281, 269 | 48, 49 | most | 3 | multi R, – |
| `announcements:` | 178 | 153 | 153 | 0 | own block |
| `heater:`, `battery:`, `motorblower:`, `ventilator:` | 173, 165, 164, 158 | 136, 97, 85, 150 | most | 64 | –, –, R A F, R A F |
| `oilpump:`, `epbrakeinc:`/`dec:`, `springbrake:`/`off:` | 148, 147, 125 | 64, 122, 96 | most | 54 | – |
| `doorstepopen:`/`close:`, `doorpermit:`, `tractionacmotor:`, `turbo:` | 103, 84, 79, 75 | 96, 67, 22, 44 | most | 3 | –, –, R A F, multi R |
| `fuelpump:`, `inverter:`, `dieselinc:`, `compressoridle:`, `radiatorfan1:`, `transmission:`, `brakingresistorventilator:` | 74 to 4 | | | | |

Never used: `fakeengine:`, `waterpump:`, `waterheater:`, `wheelflat:`,
`radiatorfan2` (unreadable, A.7). Totals: 8,283 block sites, 5,796 legacy.

`internaldata:`, DynObj's vehicle sounds: `couplerattach:`/`detach:` 1,308
`.mmd`, `slipperysound:` 1,034, `pneumaticrelay:` 896 (never plays, A.7),
`controlattach:`/`detach:` 880, `relay:` 830, the hose, gangway and heating
pairs ~801, `bufferclamp:` 504, `couplerstretch:` 326, `startjolt:` 307,
`linebreakerclose:`/`open:` 282 / 220, `ignition:` 206, `shuntfield:` 196,
`wejscie_na_bezoporow:`/`drugi_uklad:` 122, `bufferclamp_loud:` 97,
`wipertopark:`/`frompark:` 49, `engageslippery:` 27, `directionrelay_*:`
14, `shutdown:` 4. Mostly legacy (7,494 sites, 1,155 block).

`internaldata:`, Train's cab sounds: `runningnoise:` 1,112, `airsound:`
975, `brakesound:` 958 (the cab's), `buzzer:` 826, `airsound2:` 778,
`rainsound:` 766, `tachoclock:` 734, `airsound3:` 714, `radiostop:` 630,
`localbrakesound:`/`2:` 418 / 386, `airsound4:`/`5:` 374, `huntingnoise:`
332, `ctrl:` 221, `switch:` 211, `reverserkey:` 180, `pneumaticswitch:`
141, `distancecounter:` 59, `buzzershp:` 50, `resonancenoise:` 38,
`slipalarm:` 30, `ctrlscnd:` 29, `fadesound:` 1; `windsound:` never.
3,017 legacy sites, 2,413 block.

Non-sound keys there: `mechspring:` 1,099, the 30-number `soundproofing:`
481 (22 more inside a cab section, still read by DynObj), `huntingspring:`
327, `enginespring:` 91, `jointcabs:` 45.

## B.3 Inside the blocks (12,249 distinct blocks)

| inner key | blocks | | inner key | blocks |
|---|---:|---|---|---:|
| `range:` | 7,039 | | `soundend:` | 2,153 |
| `soundmain:` | 6,332 | | `offset:` | 2,132 |
| `amplitudefactor:` | 5,594 | | `soundbegin:` | 2,076 |
| `sound<N>:` (41,360 entries) | 5,143 | | `pitch<N>:` (11,125) | 1,629 |
| `placement:` | 4,235 | | `pitchvariation:` (739 of 751 are 0) | 749 |
| `amplitudeoffset:` | 3,915 | | `soundset:` | 675 |
| `crossfade:` | 3,542 | | `frequencyfactor:` / `offset:` | 627 / 379 |
| `soundproofing:` | 3,014 | | `startoffset:` | 0 |

- Shapes: table only 5,005; `soundmain` only 4,256; begin + main + end
  1,978; `soundset` 675; end + table 101; no sample 99 (the `soudmain:`
  typo); others below 62.
- Tables: 1 to 48 entries, median 6. Thresholds -10 to 16,200; 17 tables
  out of order (MaSzyna sorts them). Keys: `wheel_clatter` axles 698,
  `curve` 682, `brakesound` 549, `brake` 540, `outernoise` 474,
  `rainsound` 387, `runningnoise` 383, `tractionmotor` 265, `tachoclock`
  189, `brakecylinderinc` 165, `shuntfield` 121, `relay` 118, `engine` 114.
- `pitch<N>`: 1,629 blocks, 1,350 of them with a pitch for every entry
  (`outernoise` 351, `runningnoise` 320, `tractionmotor` 264, `engine` 114).
- `soundset:`: 1 to 16 alternatives (2 most often); `localbrakesound` 168,
  `compressor` 133, `converter` 109, `horn1`/`horn2` 70/68.
- Random sets `[ a b ... ]` elsewhere: 5,266 definitions, 2 to 14 names.
- `range:` median 90, -1 to 1,600; -1 (at the listener) 700 times, mostly
  `runningnoise`, `brakesound`, `huntingnoise`.
- `placement:` external 1,543, internal 1,254, engine 842, custom 519,
  general 68; 9 typos fall back to the default.
- `soundproofing:` most often `0.2 1.0 0.2 0.05 0.0025 0.0025` (639),
  `0 0.1 0 1 1 1` (176), `0.0001 0.01 0.0001 1 1 1` (174).
- `amplitudefactor:` median 1.2 (0 to 2,000); `crossfade:` median 85.
- Legacy numbers given: amplitude 2,416, range 2,147, frequency 577.
- `wheel_clatter:`: 2,879 axle entries at 706 sites, 4 axles most often.

## B.4 Cab control sounds

| | count |
|---|---:|
| controls carrying sounds | 16,286 (15,684 gauges, 602 lamps), in 744 `.mmd` |
| `soundinc:` / `sounddec:` | 14,983 / 14,245 controls |
| `sound<N>:` | 1,917 controls, 15,734 entries, N -10 to 51 |
| `soundon:` / `soundoff:` / a control's `soundproofing:` | 0 / 0 / 0 |
| values | 42,312 names, 2,254 random sets, 398 blocks |

Most: `cablight_sw` 541, `security_reset_bt` 364, `radio_sw` 360,
`main_on_bt` 354, `brakectrl` 351, `dirkey` 334, `mainctrl` 317. With
`sound<N>` tables: `brakectrl` 349, `mainctrl` 317, `dirkey` 291,
`localbrake` 222, `scndctrl` 146, `manualbrake` 137.

## B.5 Files

- 125,868 references (vehicle 55,474, cab controls 52,854, cab 17,540) to
  4,229 names. Written as `.wav` 4,047, `.flac` 136, no extension 43.
- 125,847 resolve (99.98 %) with MaSzyna's rule (A.3), all to `.ogg`, all
  in `sounds/` but one (`dynamic/pkp/x_401z_v1/engine.ogg`). No FLAC file
  exists. 11 names (21 references, 41 `.mmd`) are missing, among them
  `07]hasler_3.wav` (17 `.mmd`) and `697_st44-horn1/2/3`.
- Every file under `sounds/` is lower case; none differ only by case.
- The referenced set: 4,190 Ogg Vorbis files, 99.2 MB of the 311 MB
  folder; 4,172 mono, 18 stereo; 44.1 kHz 57 %, 48 kHz 23 %, 22.05 kHz
  19 %; 3.16 h in all, median 0.59 s, 90 % under 6.5 s, the longest
  `rainsound_default` 174 s. 1,610 top-level files are not referenced.
- Names: 145 carry bracketed author prefixes (`07]`, `[1007]`, `104e_3]`)
  and are plain names (only a lone `[` opens a set); 11 hold a path
  (`sip/...`).

## B.6 Content problems

| problem | where | MaSzyna's result |
|---|---|---|
| `gengwaydetach:` | 491 sites, 801 `.mmd` | no gangway detach sound |
| `soudmain:` | 99 `unbrake:` blocks | no sample; the built-in default plays |
| a `}` stuck to a value | 11 files, 37 `.mmd` | the block never closes: DynObj misses `internaldata:` (couplers, relays, `mechspring`); Train still reads it |
| no `endsounds` | `el16_v2/asf.mmd` | the same |
| a legacy entry short of numbers | `vectron_v1/vectron.mmd.inc`, 38 `.mmd` | eats the next key (`airsound2:`) |
| `pitch<N>` before its `sound<N>` | 107 blocks | the pitch is dropped |
| a key twice in one file | 25 cases, 95 `.mmd` (`runningnoise:` 64) | the last one wins |
| `sound0:[ a b ]` stuck together | IMPULS `brakectrl:`, 28 `.mmd` | the first name only |
| announcement keys without `announcements: {` | EN57AL, 7 `.mmd` | not loaded |
| `maxtachovel:`, `wtemp:`, `otemp:`, `i-compressor_ovld:` | 33, 21, 21, 20 `.mmd` | unknown to this MaSzyna |
