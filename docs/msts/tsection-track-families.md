# Track and Road Shape Families of the Global tsection.dat

Review for [tracks task 17](../tasks/tracks/17-trprofile-complex-tracks.md)
(procedural track from `tsection.dat` and TrProfiles, without GLOBAL
shapes), 2026-11-23. It describes every shape family of the global
`tsection.dat`: who made it, track or road, what it looks like and what a
profile needs to draw it. The user's answers to the findings below are
task 17's decisions 13-24. A second round of web research (2026-11-26/27)
added which sets routes still use ("Use today"), where their packages are,
and pictures; its facts are in each set's entry.

## Sources and method

- Two files: `TS_STARTER_ROUTE/GLOBAL/tsection.dat` (Build 00054, 14,629
  shapes) and `OR_PSK_NEW/GLOBAL/tsection.dat` (Build 2.15 PL, Polish and
  Hungarian additions, 7,254 shapes).
- Every shape grouped by its path geometry with
  [`extra/tsection-review/review.py`](../../extra/tsection-review/README.md)
  (single, parallel, junction, diamond, frog piece, tunnel, bridge, special,
  road), with the `_INFO` credit lines above it, its gauge value, flags,
  where the shape file exists locally and PRUDNIK's objects on it. The
  per-shape lists are in the assets-gen repository,
  `reports/complex-tracks/tsection-build54.csv` and `tsection-psk.csv`; the
  per-prefix summary in `reports/tsection-families/prefixes.txt`.
- The 311 name prefixes were grouped into sets by their credits, the
  shape-index allocation list at the top of Build 54, names, local shapes
  (textures and heights read with `assembly.py`) and web sources. Part 1
  covers the 260 rail prefixes (28 sets and small pieces), Part 2 the 51
  road-only prefixes (11 sets). Each part lists its sources.
- Caution:
  - Only part of the shapes exist on this machine: 8,221 of Build 54's
    14,629. For 14 rail sets and most small road sets there is no local
    shape, so their look comes from names and credits, and since the
    second round from documentation and pictures.
  - Most third-party sets are 2002-2010 MSTS add-ons whose sites are gone.
    Facts taken only from names or `_INFO` lines are marked so, and
    "unconfirmed" means a source could not be found or opened.
  - Heights were read without the shapes' matrices, so a part placed by a
    matrix (a contact wire) shows its untransformed height.
- Checked again after the research:
  - roads stay static in `Forced` (the code, see finding 9);
  - the XTracks `S` shapes with `TunnelShape` have no walls
    (`S1t100mStrt.s`: rails and sleepers, `SCleanTrack1/2.ace`);
  - DBTracks `f` skins carry the contact wire: PRUDNIK's own
    `TrProfile_DB3f.stf` sweeps it (`mt_cwire`) at 7.19 m.

Second round (2026-11-26/27): which sets routes still use, and pictures.
The Wayback Machine (web.archive.org) could be reached this time, with rate
limits, and was used for the authors' sites and old download hosts.
trainsim.com's file library was browsed with a free account. It now holds
the old UKTrainSim library under the same file ids, so a UKTS id in an old
readme opens at `https://trainsim.com/forums/filelib/search-fileid?fid=<id>`;
"fid N" below means that page. Nothing but pictures was downloaded. The
TrackObjs of two Polish Open Rails routes on this machine (`~/ORTS/CMK_E30`:
CMK and E30-Podkarpacie) were counted by file name.

- Closed to this round: uktrainsim.com (no longer resolves), trensim.com
  (offline after a security problem), the-train.de (403 to scripts), Elvas
  Tower's search and some of its topics (closed to guests), the 3DTrains
  forums (3dtrains.com is parked), railforums.co.uk and
  districtdavesforum.co.uk (refused automated reads). Readmes in PDFs,
  `.doc` files and installers were not opened.
- Reports: `/home/arch/tsection-families-web/reports/` (`usage.md`,
  `pictures-rail-a.md`, `pictures-rail-b.md`, `pictures-roads.md`,
  `trainsim-com.md`). `trainsim-com.md` was written last; where it and
  `usage.md` disagree, this document follows `trainsim-com.md`.
- Pictures: `/home/arch/tsection-families-web/pictures/<set>/`, each folder
  with a `SOURCES.md` (origin, archive link, certainty), and a contact sheet
  `/home/arch/tsection-families-web/reports/pictures-sheet.jpg`. They are
  third-party and stay outside the repository.
- The local `/home/arch/tsdata/global/tsection55.dat` is Build 00038 despite
  its file name (`_INFO ( Build 00038 )`), the build XTracks 3.20 shipped in
  November 2007 (<https://www.nklj.se/xtracks/documents/versions.htm>). The
  sets checked against it (`tj`, `WS`, Morris, Jennings, Europeanbahn,
  `lultube`, Lartigue) already have all their Build 54 entries there; `VT`,
  `SLW_` and `RR_` are not in it.

## Findings for task 17

Rail:

1. **PRUDNIK's track is the A namespace.** About 34,000 of its 49,052
   TrackObjs are DBTracks skins (`DB/DB30_A1t25mStrt.s`) on A-namespace
   geometry. Filters by skin name (`DB/DB3*` wood, `DB/DB2*` and `DB/DB1*`
   concrete, `DB/DR2*` GDR concrete, `DB/DB4*` steel, the `f` wire skins,
   `DB/PL*`) come before a plain `A*` filter.
2. **`TunnelShape` does not mean a tube.** About 290 shapes have the flag
   and no walls (XTracks `S`, `VT`, PSK's ghost `N` tunnels, Europeanbahn
   virtual tunnels), 70 PRUDNIK objects among them; Jeffrey Morris's 117
   `tunnel*` shapes and some ScaleRail and CYKWORKS tunnels have tubes and
   no flag. `Kind ( Tunnel )` cannot decide a tube; names must.
3. **The gauge is not in `tsection.dat`.** `SectionSize` writes 1.5 for
   standard gauge and also for ScaleRail's 30" and 36" narrow gauge; other
   sets write 0.9, 0.75, mixed values or 0. The design lays rails at the
   route's gauge, so a profile or filter must carry the gauge for about
   ten sets.
4. **The contact wire is in about 15,100 PRUDNIK track objects** (DBTracks
   `f` skins). Profiles sweep it (PRUDNIK's profiles already do); junctions
   need their own wire plan, which the design does not have.
5. **Conductor and rack rails were not in the design.** Third and fourth
   rails (UK FineScale, Europeanbahn, `tj`, CYKWORKS, LUL, probably RET),
   their side in the name; the rack sets need a centre rack rail and a rule
   for which path keeps it at points. (Answered: task 17's decision 24.)
6. **Dual gauge needs a common rail drawn once across two paths**
   (DualTracks). Every plain dual-gauge piece falls in the overlapping-paths
   group, so it reaches the `COMPLEX` member.
7. **Track spacing varies from 2.49 m to 7.62 m between sets.** A family's
   side roles (`LEFT`/`MIDDLE`/`RIGHT`) fit one spacing.
8. **More shapes should stay static** than the design's turntables,
   transfer tables and roundhouse: track with buildings or structures (RET
   stations and viaducts, CYKWORKS stations and El, the PSK depot hall and
   gates, Morris substations, the `DB2he` embankments) and things that are
   not track (Schwebebahn, Lartigue monorail, `SR_River`, ferry decks,
   invisible track).

Road:

9. **Roads stay static in `Forced` today.** For a road TrackObj without a
   template, the policy looks for a road profile named `DefaultTrack`; no
   road family has that name (the road default is `RdProfile`), and the
   texture guess runs for rail only. PRUDNIK has 13,056 road objects.
10. **Most roads are one flat strip** at y 0.1 m with all markings in a
    texture across the full width; one polyline with the same texture and
    repeat draws them exactly. 92 % of PRUDNIK's road objects are plain
    2-lane roads.
11. **The side roles do not fit roads**: lane widths 3.0-3.9 m between
    sets, 764 shapes with medians that have no path, 82 shapes whose paths
    are not listed left to right, lane add/drop pieces with two paths from
    one point. A road member stretched between the outermost paths (like
    the `COMPLEX` bed) would fit them; it is not in the design.
12. **Road junctions need their own member**: 296 junction shapes (T, X, Y,
    skew crossings, exits, merges) and 59 lane changes want a filled
    surface, kerb returns, markings that stop inside the junction, stop
    lines. PRUDNIK: 149 objects on 15 junction shapes, 47 lane changes.
13. **Case-insensitive patterns merge sets**: `fwy*` matches `Fwy*`, `dr*`
    the `DR_*` set, `pr*` the `prrw*` runways, `br*` PSK's `Br_BAB_2w/`
    folder. Road filters combine `Kind ( Road )` with longer patterns.

The user's answers (2026-11-23, task 17's decisions 13-24):

- 2: the tunnel profile is chosen by name filters.
- 3: the gauge comes from the profile's `TrackGauge`; a set of another
  gauge gets its own family.
- 4: the overhead wire comes later.
- 5: conductor rails (third and fourth) and rack rails are supported:
  ordinary profile items on plain track; in complex shapes `ConductorRail`
  and `RackRail` parts, at first only cut where they would cross another
  track; ramps and rack points fine-tuned later.
- 6: rail lines that lie on each other are drawn once (dual gauge's common
  rail, switch toes).
- 8: embankments and monorails are drawn by TrProfiles (the `bbb` route has
  similar profiles); track with buildings is procedural too, the track
  only, without the buildings; a route keeps a static shape by a `DISABLED`
  filter or on the object.
- 9: in `Forced`, road objects that no filter matches fall back to
  `RdProfile`.
- 11: the side roles stay; they were made for roads first and work, and a
  custom lane width needs a custom profile. For a 2+2 road whose centre
  marking differs from the other lane lines, new roles `CENTERLEFT`,
  `CENTERRIGHT` and `CENTER` (falling back to `MIDDLE`).
- 12: road junctions use `COMPLEX` with more part types.
- 13: filters stay case-insensitive; longer patterns and `Kind` tell such
  sets apart.

All findings are answered.

## Use today

Second round (2026-11-26/27, see "Sources and method"): does a route still
use the set, and where can its package be had? Classes:

- **widely used:** several notable routes use it, and it can be had;
- **niche:** one region or a few routes, still downloadable or used
  recently;
- **abandoned:** no route and no package found. Absence of evidence is
  weak, so this means only "not found".

CMK and E30 counts are TrackObjs by file name in `~/ORTS/CMK_E30`; E30's
are lower bounds (591 of its 1,037 world files are compressed and were
skipped). A "fid" is a trainsim.com file; it downloads with a free account.

| Set | Class | Routes that need it (examples) | Package today | For profiles |
| --- | --- | --- | --- | --- |
| A namespace: Kuju + XTracks and additions | widely used | 16 of Steam4me's 18 Australian route pages ("You must have Xtracks 3.xx or higher"); Nodo di Roma, Triveneto, Torino-Modane; PSK, CMK (30,615 `A1t*`, 3,952 `A2t*`), E30; Cerdagne | XTracks 3.20 (2007): [Steam4me](https://nekocoaster.github.io/steam4me/routes/xtracks.html), [fid 14093](https://trainsim.com/forums/filelib/search-fileid?fid=14093); 3.21 (2019): [fid 85024](https://trainsim.com/forums/filelib/search-fileid?fid=85024); 3.22 (2024): [fid 88844](https://trainsim.com/forums/filelib/search-fileid?fid=88844) | own profiles (task 17's groups) |
| DBTracks skins on A and S geometry | widely used (Germany, Poland) | PRUDNIK (about 34,000), E30 (2,680 `DB2_A*`); PSK's GLOBAL, which also serves CMK, Kocmyrzówka and lines 202, 351, 353 | seven zips on the-train.de only, registration ([dbtracks.com](https://dbtracks.com/pages/DownloadDBTracks.html); host closing, see below); US3 shapes [fid 88740](https://trainsim.com/forums/filelib/search-fileid?fid=88740); OR profiles [dbtracks-stf-profiles](https://github.com/pgroenbaek/dbtracks-stf-profiles) | own profiles, skins before `A*` |
| ScaleRail (3DTrains) | widely used (North America) | WP Feather River, LaSalle Rockford & Central, Siskiyou, Utah Belt (2026), Soo (2025), UP Gila (2025), Detroit-Toledo (2024); NS Pocahontas and Clinch Valley conversions (2026) | 3dtrains.com parked; [fid 87680](https://trainsim.com/forums/filelib/search-fileid?fid=87680) (2023, "with permission from 3DTrains"), textures fids 87686, 87687; [Elvas Tower](https://www.elvastower.com/forums/index.php?/files/category/139-scale-rail/) (2022-23); OR profiles V4.0 [fid 89793](https://trainsim.com/forums/filelib/search-fileid?fid=89793) | own profiles; the OR pack is a reference |
| UK FineScale (Tim Booth) | widely used (UK, Ireland, Australia) | Airedale, Dorset Coast (v6, 2024), South Devon Main Line, Bristol-Birmingham, Somerset & Dorset, Plymouth-Penzance (2020), New Forest / Watersnake (2023, edited with TSRE), Scottish Capital Express | v3.1.6 (2005): [fid 11889](https://trainsim.com/forums/filelib/search-fileid?fid=11889), installer [fid 88469](https://trainsim.com/forums/filelib/search-fileid?fid=88469), texture sets fids 18021, 18023, 18025, 18029; [Steam4me](https://nekocoaster.github.io/steam4me/utilities/index.html) (installer only) | own profiles, one per texture set |
| BR Rail Project (Joseph Pearson) | niche (one route) | Chiltern Route BETA (Open Rails, 2020, fid 39521) | BR_Project_Complete, 2009 beta: [fid 22268](https://trainsim.com/forums/filelib/search-fileid?fid=22268) | lighter |
| MTracks (Mark Hyams) | widely used (US, Poland) | J. H. Sullivan's routes (L&N/CSX Knoxville Sub, CNO&TP 3rd District), Milwaukee Road Aberdeen Division; CMK (about 8,150 with reskins), E30 (1,000+), PRUDNIK (97) | v1.90 [fid 66117](https://trainsim.com/forums/filelib/search-fileid?fid=66117); replacement MTracks [fid 88502](https://trainsim.com/forums/filelib/search-fileid?fid=88502) (2024); [digital-rails.com](https://digital-rails.com/mtracks/index.html) | own profiles |
| Bjorn van Steenbergen `DR_` track | abandoned | none found | none found | generic filter |
| Europeanbahn | niche (owners of its payware) | Europeanbahn's own routes: London Brighton Express, East Coast Express, West Coast Main Line, London South Coast, Cross City, Midland Main Line, Farringdon Extension (which uses which pieces: unconfirmed) | no track package; came with the products, not sold since about 2013 ([shop 2011](https://web.archive.org/web/20110418211207/http://www.europeanbahn.co.uk/shoppingcart/sh_Main.htm)) | lighter |
| Via Metrica / Sierra Menera | niche | Ferrovia delle Dolomiti (Italy, 2011, re-uploaded 2024), La Mure (France, 2007), Argentine Trasandino (Open Rails, 2024) | inside the Dolomiti package ([TSH](https://www.trainsimhobby.net/infusions/pro_download_panel/download.php?did=923)); trensim.com served it to Nov 2024 and is offline now; Steam4me's links answer 404 | own profiles (metre gauge) |
| Tram track (Ralf Riemer) | niche (Poland) | PRUDNIK (488), CMK (55 `TramBB_A`); Chiltern Route BETA ships `TramB_A` | original host gone; ships in PSK's GLOBAL ([psk.trainsim.pl](https://psk.trainsim.pl/)) | own profiles |
| Y-Tracks (Juergen Romanowski) | widely used (Poland) | E30-Podkarpacie (7,158+, its most common track), CMK (895), PRUDNIK (220); Stockholm Regional (2011) ships `Y1tHz` | [fid 61734](https://trainsim.com/forums/filelib/search-fileid?fid=61734) (2004), YTracks 02 [fid 69398](https://trainsim.com/forums/filelib/search-fileid?fid=69398) (2007); 1.81PL in PSK's GLOBAL | own profiles |
| XTracks subterranean `S` | niche | CMK (303 `S1t*`), PRUDNIK (72, mostly DBTracks names) | in every XTracks package | own profiles (track, no tube) |
| Italian track `It` (AlexB) | niche (the package); route use unconfirmed | the unfinished Faentina; SILA (Open Rails, 2023, fid 88325) asks for the IT-VT package | VT-Tracks e IT-Tracks v1.1 (2019): [ilDeposito](https://www.ildeposito.net/NuovePagine/xtracks.html), TSH | lighter |
| Rack rail (Merjan / Hanel) | niche | Snowdon Mountain Railway (2013, fid 32042); Guido Ferretti's Open Rails routes ship `MT1t*` pieces: Trento-Malè-Marilleva (2023), SILA (2023), Fiemme Valley 1960 (2024) | Z-Tracks and MT-Tracks: [fid 73586](https://trainsim.com/forums/filelib/search-fileid?fid=73586) (2009), [fid 64691](https://trainsim.com/forums/filelib/search-fileid?fid=64691) (2005), Route Builders' Edition [fid 64690](https://trainsim.com/forums/filelib/search-fileid?fid=64690) | lighter |
| XTracks narrow gauge `N` | niche | E30-Podkarpacie (144 `N1t*`), CMK (12); probably Steam4me's VR Gembrook-Cockatoo (unconfirmed) | in every XTracks package | own profiles |
| HSR slab `V4hs` (Norbert Rieger) | niche (German HSR) | NBS Köln-Rhein/Main, SFS Nürnberg-Ingolstadt-München, SFS Hannover-Berlin V3 | only inside those routes on the-train.de (closing); OR profiles and [dbtracks-extras](https://github.com/pgroenbaek/dbtracks-extras) (2025-26) | own profiles |
| R.E.T. Metro | abandoned | RET Metrolijn, the set's own route (V5 beta, 2009) | site gone; V5.0 and its SP1 only in the [Wayback Machine](https://web.archive.org/web/20110528032057/http://www.busfoto.nl/40jaarmetro/RMversie5-0.exe) | generic filter |
| Chris Jennings `NRa` | abandoned | none found | none found | generic filter |
| Jeffrey Morris | abandoned | none found | none found | generic filter |
| UK narrow gauge `ukn` | niche (thin evidence) | Corris No. 3 scale patch (2002, "requires UK_NG track"); Reg Furniss' rework (2012) for a Talyllyn 1940s route not found released | ships in XTracks; rework [fid 28317](https://trainsim.com/forums/filelib/search-fileid?fid=28317) | lighter |
| Metre gauge 3rd rail `tj` | niche (one route) | La ligne de Cerdagne V2 (2005) | inside the route: [funtrain.net line 27](https://api.funtrain.net/lines/27/) (`Cerdagne.exe`, 69 MB) | own profiles |
| CYKWORKS | niche (dormant) | Chicago Elevated Railways (2003; fids 60725, 59386, 57474) | Track Pack 1 [fid 59089](https://trainsim.com/forums/filelib/search-fileid?fid=59089), 2.0 [fid 59401](https://trainsim.com/forums/filelib/search-fileid?fid=59401); ships in XTracks | lighter |
| XTracks virtual tunnel `VT` (OBY) | widely used (Italy) | Siracusa-Modica (2009), Triveneto (2012), Nodo di Roma (2013, 2017), Italia 28 V3 (2015), Torino-Modane 1970 (2018) | VT-Tracks e IT-Tracks v1.1: [ilDeposito](https://www.ildeposito.net/Files/Vari/VT_Tracks-IT_Tracks.zip), TSH | own profiles (track only) |
| DualTracks (William Adams) | niche | La Crosse Division (Milwaukee Road, 2003, fid 58902); MBTA (2004) and SA Adelaide, Hills & South East (2025) ship `D1tPnt*` pieces | v1.1 [fid 61072](https://trainsim.com/forums/filelib/search-fileid?fid=61072), Reference Route [fid 57842](https://trainsim.com/forums/filelib/search-fileid?fid=57842); ships in XTracks | lighter |
| 600 mm Feldbahn `lsfb` | abandoned (no released route) | none; the Polish Podlasie project plans it for the Hajnówka forest railway | PSK's GLOBAL only | generic filter |
| Schwebebahn `WS` (Sjoerd Ydema) | niche (its own route) | Wuppertal Schwebebahn (Ydema, 2007) | the route: [HCC!trainsim](https://trainsim.hcc.nl/downloads/microsoft-trainsimulator/routes-voor-microsoft-train-simulator/154-wuppertal-schwebebahn-v1/file), [fid 66942](https://trainsim.com/forums/filelib/search-fileid?fid=66942) | generic filter (`DISABLED`: not track) |
| LUL tube (Richard Garniss) | abandoned | none found (Ydema's Northern Line does not use it) | none found | generic filter |
| Steven Masters `F` | abandoned | none found | ships in XTracks 3.19 | generic filter |
| Lartigue monorail (Andrew Gadd) | abandoned | none (a 2005 work-in-progress post only) | none found | generic filter (`DISABLED`) |
| PSK small pieces | niche (Poland) | PRUDNIK (1), CMK (58 `Wk_brama`), E30 (2) | PSK's GLOBAL (PSK 7.3, updated 2026-11-24) | lighter |
| `Osmannoro_OML` (Raffaele Fiore) | abandoned | none found | none found | generic filter |
| Kuju roads + NewRoads additions | widely used | every MSTS route can use them; PRUDNIK (9,586), CMK, E30 | MSTS; NewRoads 4.0 (next row) | own profiles |
| NewRoads local roads | widely used | Steam4me's routes (NewRoads 3.2 or 4.0 required), PRUDNIK (2,975), CMK (`Urban` 5,724) | 4.0 installer [fid 6254](https://trainsim.com/forums/filelib/search-fileid?fid=6254), [fid 69884](https://trainsim.com/forums/filelib/search-fileid?fid=69884), files only [fid 80272](https://trainsim.com/forums/filelib/search-fileid?fid=80272); Route Builders Kits on [Steam4me](https://nekocoaster.github.io/steam4me/routes/newroads.html); "Grundversion" on the-train.de | own profiles |
| NewRoads highways and freeways | widely used | PRUDNIK (194), CMK (`fwy` 162, `hwy` 21) | as NewRoads | own profiles |
| German Autobahn `BAB` (Norbert Rieger) | niche | CMK (284), PRUDNIK (225); non-German routes ship `bab2l1w*`: Gotland (2012), Dorset Coast v6 (2024), SRv2 Phase 3 (2020), Oro (2026); German routes not identified | in NewRoads 4.0; Rieger's variants listed on [dbtracks.com](https://dbtracks.com/pages/DL_bab.html), hosted on the-train.de (closing) | own profiles |
| ScaleRail roads (ScaleRoad) | widely used (North America) | Utah Belt, Soo, UP Gila, Detroit-Toledo, KCOS, Monon-2, LaSalle Rockford & Central, Siskiyou | [fid 87684](https://trainsim.com/forums/filelib/search-fileid?fid=87684) (2023); textures in "Saluda Road Textures" [fid 69357](https://trainsim.com/forums/filelib/search-fileid?fid=69357); [Elvas Tower file 3208](https://www.elvastower.com/forums/index.php?/files/file/3208-scale-roads/) | own profiles |
| J.A. Norton `SLW_` | abandoned | none found | none found | generic filter |
| Dale Rickert `RR_` | abandoned (route withdrawn) | probably The Siskiyou Route, pulled by its author | none; the route's last installer only in the Wayback Machine | generic filter |
| Bjorn van Steenbergen `DR_Rd_Type` | abandoned | none found | none found | generic filter |
| R. Fiore roads | abandoned | none found | none found | generic filter |
| Kraków-Kocmyrzów `KK_Alpl` | niche | Trasa Kocmyrzów (Linia 111; Tabore, 2012; timetables 2021), PRUDNIK (76) | inside the route: [pkor.trainsim.pl](https://pkor.trainsim.pl/trasa-krakow-kocmyrzow/) | own profiles |
| Magnetic platforms (Graham Pitt) | widely used (UK first) | YCR, West Anglia Main Line, London-Southampton, South Devon, Manchester Metrolink, BrisCard, Dabley; 58 uploads ship the shapes, e.g. Mumbai-Amritsar, High Speed 1, New Forest V2, NSW Joppa Junction (2025) | Kits I-IV (2002): [fid 1186](https://trainsim.com/forums/filelib/search-fileid?fid=1186), [fid 1187](https://trainsim.com/forums/filelib/search-fileid?fid=1187), [fid 1210](https://trainsim.com/forums/filelib/search-fileid?fid=1210), [fid 1211](https://trainsim.com/forums/filelib/search-fileid?fid=1211) | own profiles |

For profiles:

- **Own profiles** (sets in routes people run): A/XTracks with the DBTracks
  skins, ScaleRail (track and roads), UK FineScale, MTracks, Y-Tracks, `VT`,
  V4hs, the Kuju roads and the three NewRoads families, BAB, the magnetic
  platforms, the Riemer tram, `KK_Alpl`, `tj`, Via Metrica, `S`, `N`.
- **Lighter** (one family or a few filters, after the first group):
  Europeanbahn, `It`, `ukn`, CYKWORKS, the PSK pieces, BR Rail, rack rail,
  DualTracks.
- **One generic filter is enough** (abandoned, or not track): `DR_` track
  and roads, RET, Jennings, Morris, LUL tube, Lartigue, Schwebebahn, `F`,
  `lsfb`, `SLW_`, `RR_`, Fiore's roads and `Osmannoro_OML`.

The round's first report (`usage.md`) had the magnetic platforms, BR Rail,
rack rail and DualTracks as abandoned; trainsim.com's library showed their
packages and routes. The Schwebebahn route is downloadable too, but it
stays `DISABLED`.

**Hosting risk.** the-train.de is the only host found for DBTracks, the BAB
packages, NewRoads 4.0 "Grundversion" and the German routes with V4hs. The
TSSF forum announced on 28 Feb 2026 that "das Forum und die dazugehörige
Webseite" close on 31 Dec 2026
(<https://tssf.eu/forum/thread/12644-wichtige-mitteilung-schlie%C3%9Fung-abschluss-von-forum-und-webseite-zum-31-12-2026/>).
In a reply the same admin says the costs "für TSSF & The-Train" are no
longer justified. So the-train.de will probably close with the forum,
although the post does not say so in words.

## Part 1. Rail families

Scope: every name prefix of `rail.txt` (260 prefixes), grouped into 28
sets plus a table of small pieces (Raffaele Fiore's one shape, PSK's
single objects and one-offs of the larger sets). Mixed track-and-road sets (ScaleRail,
Bjorn van Steenbergen's `DR_`, the tram set) are described whole; their
road-only prefixes are named but their road shapes are reviewed in the road
section.

How the numbers were made:

- Shapes: entries in Build 00054 (`TS_STARTER_ROUTE`) / Build 2.15 PL
  (`OR_PSK_NEW`), from the given CSVs. A prefix was assigned by the longest
  matching prefix of `rail.txt` and `road.txt` (`SR_River.s` is the `SR`
  prefix); the per-prefix counts match `rail.txt`.
- Found locally: distinct file names of the family found in at least one of
  the four GLOBAL/SHAPES folders, "of" the distinct names in both files.
- PRUDNIK objects: the CSV's `uses` (TrackObjs counted by `SectionIdx`, so a
  DBTracks object such as `DB/DB30_A1t25mStrt.s` counts with the A shape
  whose geometry it uses).
- Authors: the `_INFO` lines and the shape-index allocation list in the
  `_Skip` block at the top of Build 54's `TrackShapes` ("Shapes 26500-26632
  Mark Hyams MTracks", ...), plus the developer list at the top of the file.
- Gauge: MSTS writes `SectionSize ( 1.5 ... )` for standard gauge, so the
  first value is a class, not the gauge. The real gauge below comes from
  the set's documentation or name; the SectionSize value is in brackets.
- Group numbers are the review groups (1 single, 2 parallel, 3 junction,
  4 diamond, 5 overlapping/frog piece, 6 tunnel, 7 bridge, 8 special,
  9 road, 0 sections missing), Build 54 unless the set is PSK-only.
- "Look" is from local shapes (LOD 0 triangles, textures and their heights,
  read with `extra/tsection-review/assembly.py`; textures converted with
  `--aceconv`) where the set exists locally, else from names and sources.
  Heights are vertex values in shape space; the shapes' matrices were not
  applied, so a part moved by a matrix (a wire, a sunk rail) shows its
  untransformed height.

Sources used for many sets:

- XTracks documentation (Okrasa Ghia, v3.20, November 2007):
  <https://www.nklj.se/xtracks/> with `documents/credits.htm`,
  `documents/t_info.htm` (name system, gauges, spacings),
  `documents/dualtracks.htm`, `documents/versions.htm`.
- Steam4me guides: XTracks <https://nekocoaster.github.io/steam4me/routes/xtracks.html>,
  Via Metrica <https://nekocoaster.github.io/steam4me/routes/via_metrica.html>,
  utilities list (UK FineScale v3.1.6) <https://nekocoaster.github.io/steam4me/utilities/index.html>,
  ScaleRail dummy nodes <https://nekocoaster.github.io/steam4me/tutorials/SR_zero_node.html>.
- The standardized tsection.dat project: <https://github.com/openrails/tsection-temp>
  (builds 47 to 60) and <https://github.com/perpetualKid/tsection.dat>.
- Local: the Build 38 readme `/home/arch/tsdata/global/tsection_38_readme.txt`
  (lists the packages covered then: XTracks 3.18, DualTracks 1.1, MTracks
  1.9, UK FineScale 3.1.6, YTracks 1, ScaleRail beta, STracks 1.0, RET Metro,
  Europeanbahn, CykWorks, NewRoads 4.01, magnetic platforms) and the
  CYKWORKS Track Pack 2.0 readme `/home/arch/tsdata/global/textures/readme.txt`.
- DBTracks: <https://dbtracks.com/>, <https://dbtracks.com/pages/DBTracks10.html>,
  <https://dbtracks.com/pages/DBTracks20b.html>, and the Open Rails
  profiles <https://github.com/pgroenbaek/dbtracks-stf-profiles>.
- Second round: the Wayback Machine and trainsim.com's file library ("fid
  N", see "Sources and method"); reports and pictures under
  `/home/arch/tsection-families-web/`.

Most third-party sets are 2002-2010 MSTS add-ons whose sites are gone. The
first round's web search found little beyond the pages above; the second
round added packages, routes and pictures for most sets. Facts taken only
from names or `_INFO` lines are marked so.

### 1. Summary

| Family | Name prefixes | Kind | Gauge (SectionSize) | Shapes B54 / PSK | Found locally | PRUDNIK objects | What a profile needs |
| --- | --- | --- | --- | ---: | ---: | ---: | --- |
| A namespace: Kuju + XTracks and additions | `A` (DBTracks skins `DB/<skin>_A*`) | track | 1.435 (1.5) | 1,729 / 1,638 | 1,642 of 1,967 | 34,898 | task 17 groups; skin filters (`DB3*`, `DB2*`, `f` = contact wire), `Concrete`, `_D`, `Tun`, `Brdg` |
| ScaleRail (3DTrains) | `SR_`, `SR_XtCrv_c_`/`_w_`, `SR_XtEnd_c_`/`_w_`, `SR_XtBrg_n_`, `SR_Turntable_c_`/`_w_`, `SR_InvisiTrack_`, `SRn30_`, `SRn36_`, `SR`; roads `SR_Rural_` ... `SR_Parkway_` | track and road | 1.435; 30" and 36" narrow (all 1.5) | 2,763 + 739 road / 250 | 2,996 of 3,496 | 0 | looks `c`/`w`/`y`/`n`; tie layer; bridge guard rails; tall tunnel walls; gauge per profile for `SRn30`/`SRn36` |
| UK FineScale (Tim Booth) | `ukfs*` (118 prefixes) | track | 1.435 (1.435) | 1,115 / 0 | 0 | 0 | 3.413/4.632/5.242 m spacing; 3rd/4th rail; check rails; catch points; slips; curved turnouts |
| BR Rail Project (Joseph Pearson) | `BR_*` (18 prefixes) | track | 1.435 (1.435) | 1,281 / 0 | 0 | 0 | 3.413 m spacing; look suffixes `a b g w d`; 53 buffers |
| MTracks (Mark Hyams) | `M`, `M_25f_`, `M_Tun_` | track | 1.435 (1.5) | 445 / 569 | 534 of 614 | 97 | 4.27 m and 7.62 m spacing; US frog numbers; `Concrete` (PSK) |
| Bjorn van Steenbergen | `DR_tA`, `DR_nA`, `DR_rA`, `DR_yA`, `DR_sA`, `DR_hA`; road `DR_Rd_Type` | track and road | 1.435 (1.5) | 797 / 797 (703 track, 94 road) | 0 | 0 | six looks by letter; 5 m spacing; ROAD members for `DR_Rd_Type` |
| Europeanbahn | `eb_`, `eb_A`, `EB_` | track | 1.435 (1.5) | 437 / 0 | 293 of 433 | 0 | 3rd/4th rail by side (`i`/`o`); flat and round tunnels, wired tunnels; virtual tunnels |
| Via Metrica / Sierra Menera | `VM`, `VD` | track | 1.0 (1.0); `VD` 0.5 | 400 / 0 | 0 | 0 | metre-gauge profile; looks `C`, `Ox`; tunnels `Rtun` |
| Tram track (Ralf Riemer) | `Tram_A`, `TramB_A`, `TramBB_A`, `TramZ_A` | track in road | 1.435 (1.5) | 92 / 92 | 89 of 92 | 488 | paved bed, ballast bed, kerb; grooved rail; tram COMPLEX without ties |
| Y-Tracks (Juergen Romanowski) | `Y`, `y` | track | 1.435 (1.5) | 66 / 86 | 84 of 86 | 220 | 1:9 points, frog pieces (`Hz`), slips; wood, PSK concrete |
| XTracks subterranean S | `S` | track (tunnel flag, no tube) | 1.435 (1.5) | 143 / 121 | 85 of 143 | 72 | track only although `TunnelShape` |
| Italian track (AlexB) | `It` | track | 1.435 (1.5) | 200 / 0 | 0 | 0 | 3.555 m spacing; S60UNI points; `Sub` tunnels |
| Rack rail (Merjan / Hanel) | `MT`, `Z`, `ZG`, `ZH`, `ZI` | track | 1.0 (1.0) | 199 / 199 | 199 of 199 | 0 | metre gauge; centre rack rail; grass beds |
| XTracks narrow gauge | `N`, `NA`, `I` | track | 0.861 (0.9) | 173 / 168 | 168 of 192 | 0 | 3 ft gauge, 2.991 m spacing; PSK ghost tunnels |
| HSR slab track (Norbert Rieger) | `V4hs` | track | 1.435 (1.5) | 160 / 160 | 0 | 0 | slab track (no ballast, no ties); HSR tunnel tubes |
| R.E.T. Metro (Roland Bal) | `Ret*` (36 prefixes) | track, stations | 1.435 (1.5) | 147 / 147 | 0 | 0 | metro track, viaducts, tunnels; stations static |
| Chris Jennings | `NRa`, `NRA` | track (mostly tunnels) | 1.435 (1.5) | 145 / 145 | 0 | 0 | round, divided, half-round tunnels |
| Jeffrey Morris | `tunnel`, `switch`, `substation` | track, tunnels, structures | 1.435 (1.5) | 141 / 141 | 0 | 0 | tunnels by name (no flag); structures static |
| UK narrow gauge (Tim Bridge) | `ukn`, `uknA` | track | 0.7175 (0.75) | 127 / 127 | 127 of 127 | 0 | 2 ft 4 in gauge, 2.4925 m spacing |
| Metre gauge with 3rd rail (Etienne Bruère) | `tj`, `tj1txoverA` | track | 1.0 (1.0) | 100 / 100 | 0 | 0 | conductor rail by side (`p`/`u`, `i`/`o`); pit |
| CYKWORKS subway and El (Christopher Cyko) | `C`, `L`, `Cp`, `Croswab`, `Cperoswab`, `Cpbasbr`, `LHalOR` | track, elevated, subway | 1.435 (1.5) | 84 / 0 | 84 of 84 | 0 | 3D ties without ballast; third rail; El structure; subway box |
| XTracks virtual tunnel (OBY) | `VT` | track (tunnel flag) | 1.435 (1.5) | 77 / 0 | 0 | 0 | track only (unconfirmed: no tube) |
| DualTracks (William Adams) | `D` | track | 1.435 + 0.861 (0.9 1.5) | 71 / 53 | 53 of 71 | 0 | three rails, common rail left or right |
| 600 mm Feldbahn (Lutz Schwerthelm) | `lsfb` | track | 0.6 (0.5/0.75/0.9) | 0 / 53 | 51 of 53 | 0 | 600 mm profile, bed at ground level |
| Schwebebahn (Sjoerd Ydema) | `WS` | other (suspended monorail) | none (0.5) | 36 / 12 | 0 | 0 | not a track profile: `DISABLED` |
| LUL tube (Richard Garniss) | `lultube_*` (11 prefixes) | track in tube tunnels | 1.435 (1.435) | 30 / 30 | 0 | 0 | circular tube; 17 PSK entries without sections |
| Steven Masters F (carfloat) | `F` | track (ferry deck) | 1.435 (1.5) | 27 / 14 | 14 of 27 | 0 | rails only; `DISABLED` or a rails-only profile |
| Lartigue monorail (Andrew Gadd) | `LM`, `Lm` | other (monorail) | none (mixed) | 24 / 0 | 0 | 0 | not a track profile: `DISABLED` |
| Small sets (Fiore, PSK pieces) | `Osmannoro`, `hunBuffer`, `OdbW_`, `OdbZ_`, `Wk1Pe_`, `Wk`, `Wk_brama_wj`, `_depo-`, `a`, `DB2_a` | track, buildings | 1.435 (1.5) | 1 / 16 | 10 of 17 | 1 | mostly `DISABLED`; the small-sets table also lists one-offs of the families above |

### 2. Families

#### A namespace: Kuju originals, XTracks and additions

The geometry review of this namespace (groups, counts, PRUDNIK's complex
shapes, the 7.5° family) is in the task 17 doc, "Review: GLOBAL tsection.dat,
A namespace"; it is not repeated here. This entry is the set-level view.

- Source. One shared name space, not one package:
  - MS/Kuju originals: shapes 0-250 (173 in this prefix) plus 7 shapes
    "initially inactive, activated by Okrasa Ghia" (allocation list).
  - XTracks by Okrasa Ghia, version 3.20 (November 2007), "pretty much
    the work of Steven Masters and Norbert Rieger" by then (XTracks
    versions page). Downloads and the end-user installer are on the Steam4me
    XTracks page and on trainsim.com (fids 14093, 11854, 11857). High
    confidence.
  - 3.20 was not the last version. Robert D. Lewis "and contributers"
    released 3.21 (2019-11-23, "Includes build #54"; fids 85024-85026) and
    3.22 (2024-05-28; fid 88844 installer, fid 88845 shapes only). The
    standardised tsection builds 38 to 60 are on trainsim.com too (build 60:
    fid 88841, Derek Morton, 2024-05-26). eric's "ATracks" replacement
    shapes (fid 88570, "SD files added April 2025") are accepted by some
    routes in place of XTracks ("ATracks or Xtracks", fid 87134).
  - Contributors inside `A`, by the Build 54 allocation ranges (shapes):
    Steven Masters 380 (incl. 10 bridge pieces), Norbert Rieger 260
    (1.5°-3.0° turnouts, 4.7° and 7.5° additions, 300 m curves plain and
    `Concrete`, 6.5 m round tunnels, `A2t` tunnel switches), Okrasa Ghia
    274 (standard gauge, concrete, tram and scale points, experimental
    three-way/scissors), Stephen Thomas 180 (short straights, extra curves
    and points), MS/Kuju 180, JW Titus 106 ("non-default curves and
    switches", yard switches, single/double slip lead pieces `_SL1` ...),
    Jeffrey Kraus-Yao 102, Szilárd Futó 64 (MÁV Hungarian switches
    `A1tPntMAV*`), Paul Andrews 48 (STracks: 35-70 m tight curves), René
    Wiggerts 36 plus Okrasa Ghia/Silvan Schwab (`Concrete` shapes), Dietmar
    Jülich 29 (straights, curves, switches, slips, crossovers), Jeffrey
    Morris 18 (`A1t62r*tun`, 1-4 tracks), Raffaele Fiore 15 (`_IDP`
    10- and 12-track straights), William Adams 12 (DualTracks helpers),
    Teemu Saukkonen 11 (`7Bridge`, carfloat), Jared Singletary 3, a few
    others with 1-2 shapes.
  - PSK 2.15 additions (245 `A` names not in Build 54; its header reads
    "Xtracks 3.20 - selected, Ytracks 1.81PL, Mtracks 1.902PL, UZK 3.18"):
    wooden variants `*_D` ("Wood Tracks and Switches", "Wood Track Pieces",
    "Wood Switches", "small wood straight", about 165), concrete pieces
    ("NEW Concrete small pieces", "Concrete Track pieses", "Concrete
    curves", "experimental switches", about 50), Hungarian turntable
    pieces `A1t*Fordito*` ("Fordito elemek", 20), long multi-track
    straights `A4t`/`A8t700mStrt` ("odcinki wielotorowe - dlugie"),
    `A2t750r*Tun`, and the buffer `A1tbuffer-PL_1`
    ("kozly oporowe"). PRUDNIK uses 374 objects on these indices (93 on
    `A1tbuffer-PL_1`). "UZK 3.18" is ÚZK 3.18/3.20 ("Úpravy Základního
    Kolejiva", Czech, 2008), a re-texture of XTracks geometry, not a new
    family; its author's site is gone (second round).
- DBTracks reskins (PRUDNIK). Norbert Rieger's DBTracks re-model the `A`
  assortment with German details: "3D sleepers/ties, switch operating
  mechanisms, ... integrated overhead wires, old, rusty track variants"
  (dbtracks.com; Norbert Rieger is named as the author in
  <https://github.com/pgroenbaek/dbtracks-site>). DBTracks 2.0 Beta covers
  "all 'A' tracks" and an `s1/s2` list (probably the S namespace). The skin is the file-name prefix; the geometry is
  the A shape's `SectionIdx`. Skins on PRUDNIK by file name, with the
  DBTracks 1.0 meaning (<https://dbtracks.com/pages/DBTracks10.html>):

  | Skin | PRUDNIK objects | DBTracks meaning |
  | --- | ---: | --- |
  | `DB30` | 10,947 | wooden ties, rusty rails |
  | `DB3f` | 9,472 | wooden ties, with catenary |
  | `DB3` | 4,511 | wooden ties, brown ballast |
  | `DB2f` | 2,471 | old concrete ties, with catenary |
  | `DB30f` | 2,293 | wooden ties, rusty, with catenary |
  | `DB2` | 1,940 | old concrete ties, brown ballast |
  | `PL30`, `PL3`, `PL4`, `PL2`, `PL40` | 563 | not DBTracks names; PSK's own skins (unconfirmed). `PL30` uses `DR_Track2`/`DR_Rails2` textures |
  | `DB21f` | 382 | short (2.3 m) concrete ties, used, catenary |
  | `DB1f`, `DB1` | 483 | new concrete ties (with catenary / without) |
  | `DR2`, `DR20`, `DR2f`, `DR20f` | 316 | GDR-style concrete ties, brown ballast (rusty `0`) |
  | `DB23f`, `DB22f` | 181 | wooden / concrete ties on grey ballast, catenary |
  | `DB2he` (on MTracks geometry, `_emb_z`) | 63 | not in the DBTracks list; track with an embankment (`terrain.ace` down to -16.75 m) |
  | `DB2br`, `DB40`, `DB4`, `A1t2mFordito*`, `A1t_Buffer_new` | 75 | `br` bridge (unconfirmed), `4`/`40` steel ties |
  | `../../track_db/lalek/`, `DB_Lalek/` double slips | 231 | `DB30_A1tDblSlip7_5d_Lalek`: a separate 7.5° double slip shape (author unconfirmed) |

  About 15,100 PRUDNIK objects use an `f` skin: the overhead contact wire is
  part of the track shape. 70 `DB30*` objects (`DB/DB30_A1t100mStrt.s` ...)
  sit on S-namespace indices (subterranean track, `TunnelShape`, no tube).

  DBTracks today: "Alle DBTracks-Pakete wurden in sieben ZIP-Dateien
  zusammengefasst und sind jetzt auf the-train.de verfügbar", registration
  required (<https://dbtracks.com/pages/DownloadDBTracks.html>). That is the
  only host found, and it will probably close at the end of 2026 (see "Use
  today"). The US3 shapes and textures were "rescued after the loss of
  Norbert's website" (fids 88740, 88741, 88743, 2024). The Open Rails
  profiles (also fid 89703) and
  <https://github.com/pgroenbaek/dbtracks-extras> are maintained "In memory
  of Norbert Rieger". PSK 7.3's readme: its GLOBAL holds "pakiet torów i
  tekstur DB Tracks (© 2013 by Norbert Rieger)" and lets the Polish routes
  CMK, Kocmyrzówka and lines 202, 351 and 353 run.
- Shapes. 1,729 / 1,638; 1,642 of 1,967 names local (OR_PSK_NEW,
  TS_STARTER_ROUTE, tsdata; 168 in MSTS). PRUDNIK: 34,898 objects.
- Kind and gauge: track, 1.435 m (1.5); 3 shapes 0.9 (DualTracks helpers);
  11 PSK entries without sections.
- Contents: see the task 17 doc's table (groups 1-9). Name tokens that carry
  a look: `Concrete`, `_D` (PSK wood), `Mnl`, `Div`, `Tun`, `RndTun`, `Brdg`,
  `7Bridge`, `Wtr`, `Tram`, `Yrd`, `MAV`, `Fordito`.
- Look (looked at `A1t25mStrt`, `A1t25mStrtConcrete`, `A1t100mStrt_D`,
  `A1tbuffer-PL_1`, `DB/DB30_A1t25mStrt`, `DB/DB3f_A1t25mStrt`,
  `DB/DB2f_A1t1000r1d`, `DB/PL30_A1t500mStrt`): Kuju style is a flat 5 m bed
  at y 0.20 with ties painted on (`ACleanTrack1`, `Concrete1` for
  `Concrete`, `ACleanTrack1_D` for `_D`) and 3D rails to y 0.325
  (`ACleanTrack2`). DBTracks: a ballast shoulder layer (`DB_Track3s`,
  ±2.6 m, y -0.02..0.15), an alpha-tested sleeper layer (`DB_Track3`,
  ±1.7 m, y 0.11..0.14), rails with modelled fastenings (`DB_Rails30w`);
  `f` skins have extra geometry in the rail texture up to y 1.0 in shape
  space (the wire is likely placed by a matrix; not checked further).
- Profile needs: as in the task 17 doc (`MAIN`, side roles, `COMPLEX` for
  groups 3-5, tunnel and bridge families). Added by this review:
  - filters by skin before the plain `A*` filter: `DB/DB3*` wood,
    `DB/DB2*`/`DB/DB1*` concrete, `DB/DR2*` GDR concrete, `DB/DB4*` steel,
    the rusty `*0` skins, `DB/PL*`;
  - `DB/*f_*` profiles need the contact wire (a `Sweep` at wire height); on
    junctions the wire needs its own plan (not in the design);
  - `DB2he_*_emb_z`: track plus embankment polylines, or `DISABLED`;
  - the 70 `DB30` objects on S indices must not get a tube from
    `Kind ( Tunnel )` (see open points).

#### ScaleRail (3DTrains)

- Source: Marc Nelson, 3DTrains ScaleRail (`_INFO(http://www.3dtrains.com/scalerail)`;
  allocation "Marc Nelson Scale Rail/Road"; developer list). "A system of
  enhanced track shapes and textures for route-building in MSTS", public
  beta 1.7 (Steam4me ScaleRail page); the last public beta was 1.77 (about
  2013-15). North American prototype; the WP Feather River route and many
  others use it (see "Use today"). An Open Rails profile pack, "ScaleRail
  Track Profiles V4.0" (pschlik, Feb 2025, fid 89793), covers "wood ties,
  concrete ties, modern concrete ties, wood bridges" and was tested on six
  routes (<https://www.trainsimcommunity.com/mods/c6-open-rails/i5843-scale-rail-track-profiles-v40>).
  Open Rails has permission to publish routes that contain ScaleRail and
  ScaleRoad (<https://openrails.org/contribute/building-models/>). 22 shapes
  are Jeffrey Kraus-Yao's "ScaleRail additions". High confidence for the
  set, medium for details.
- Packages: 3dtrains.com is a parked domain. trainsim.com has "Scalerail by
  3DTrains" (fid 87680, 2023, "Uploaded with permission from 3DTrains"),
  Standard and Common Textures (fids 87686, 87687), Update 177a (fid 87688),
  texture variants and switch targets. Elvas Tower's "Scale Rail" category
  has the track, Update 177 and textures, uploaded by Genma Saotome in
  2022-23 (<https://www.elvastower.com/forums/index.php?/files/category/139-scale-rail/>).
  Cajon Pass, Pacific Surfliner and Marias Pass are ScaleRail for RailWorks,
  another product. TS_STARTER_ROUTE ships the SR shapes, but its world files
  place none.
- Shapes: 3,502 / 250 (2,763 track, 739 road). Local: 2,996 of 3,496 names,
  all in TS_STARTER_ROUTE (the PSK file lists 250 SR shapes that are not in
  OR_PSK_NEW). Textures are not in the GLOBAL folders: `SR_Track_c1a-c3a` in
  `tsdata/routes/bbb`, `SR_Track_w1a-w3a` in the MSTS test routes.
  PRUDNIK: 0.
- Kind and gauge: track and road. Standard gauge (rails at ±0.82 outer
  edge); `SRn30_`/`SRn36_` are 30" (0.762 m) and 36" (0.914 m) narrow
  gauge by their `_INFO` ("3DTrains ScaleRail 30" Narrow Gauge - Petina
  Ties" / "- Dark Ties"), yet written with SectionSize 1.5.
- Road prefixes (road section): `SR_Rural_`, `SR_Town_`, `SR_City_`,
  `SR_Urban_`, `SR_Dirt_`, `SR_Hiway_`, `SR_Inter_`, `SR_Access_`,
  `SR_Country_`, `SR_Parkway_`.
- Contents (rail, Build 54): single 1,767, parallel 474, junction 248,
  diamond 14, tunnel 242, special 18. Name system
  `SR_<n>t<Type>_<look>_<size>`:
  - looks: `c` concrete ties, `w` wood ties (Steam4me), `y` a third look
    only on 1-7 track straights at 3.9624 m (13 ft) spacing and a few
    switches (yard, unconfirmed), `n` rails only (bridge decks,
    `SR_2tStr_n_`); narrow gauge `p` (patina ties) and `w` (dark ties);
  - types: `Str`, `Crv` (1-4 tracks), `Yrd` (yard curves with position
    letters A-Z), `Swt` (switches; tokens `m`, `a`, `ia`, `im`, `_Full`,
    `_Ext`, `_Long`, `_Div`; meaning of `m`/`a`/`i` unconfirmed), `Wye`,
    `Xvr` (diamonds 2.5° to 90°), `End`, `Eru`, `Skw` (skewed ends), `Eng`
    (rails only), `Dmy` (20 m "zero degree" dummy nodes), `Derail`,
    `Buffer`, `Xng` (level crossings), `Tun` (1 and 2 tracks), `Brg`
    (bridges), `Turntable` (37 m), `InvisiTrack`;
  - `SR_XtCrv_*`/`SR_XtEnd_*`/`SR_XtBrg_n_`: one track each of multi-track
    curves, by position letter (A, B, D ... H) with slightly different
    radii (590.03 m, 595.015 m, ...): "ScaleRail 1/5/10 Degree Multi-Track
    Sections";
  - `SR_River.s`: a 100 m water plane (`SR_Watertop.ace`) with two crossing
    paths ("ScaleRiver"), not track.
- Look (looked at `SR_1tStr_c_000_6m`, `SR_1tStr_w_000_6m`,
  `SR_1tYrd_w_0250r05dA`, `SR_1tTun_c_05dL`, `SR_1tBrg_n_005m`,
  `SR_1tBrg_c_050m`, `SR_2tStr_n_005m`, `SR_XtCrv_*`, `SR_InvisiTrack_010m`,
  `SR_River`): three layers. Ballast `*1a` 4.88 m wide from y -0.20 to 0.16;
  an alpha-tested tie layer `*2a` 2.6 m wide at y 0.16-0.21 (flat ties
  with tie plates painted); 3D rails `*3a` to y 0.33. Concrete ties are
  light grey, wood ties dark. Bridges: ballasted deck (`c1d`/`c2d`) or open
  (`n`), both with inner guard rails at ±0.54 m (`*3b`). Tunnels: walls
  `c7d` up to y 8.2 m, own floor and tie textures.
- Profile needs:
  - `MAIN` per look (`SR_*_c_*`, `SR_*_w_*`, `SR_*_y_*`), side roles for
    2-7 tracks at 4.985 m and 3.9624 m spacing;
  - `COMPLEX` for `Swt`, `Wye`, `Xvr` (up to 90°);
  - bridges: a family with guard rails inside the running rails, ballasted
    (`Brg_c`, `Brg_w`) and open (`Brg_n`, `XtBrg_n`, `Str_n`);
  - tunnels: tube polylines about 8 m high;
  - narrow gauge `SRn30_`/`SRn36_`: the profile must set the gauge (the
    tsection value is 1.5);
  - `DISABLED`: `Turntable`, `SR_River`; `InvisiTrack` and `Dmy` need an
    empty profile or `DISABLED`;
  - level crossings `Xng`: track profile, crossing surface later.

#### UK FineScale (Tim Booth)

- Source: Tim Booth, UK FineScale Track Project (developer list:
  "Tim Booth (UK Fine Scale Track)", ukfs.trainsimfiles.com). Steam4me:
  "UK-standard track with a different rail profile", it "doesn't match or
  join with standard MSTS track", installer v3.1.6. Additions by Jeffrey
  Kraus-Yao (65 shapes: two-track `_11a`/`_11b`/`_11c` pieces at Kuju
  spacing and a dummy) and Dave Babb (2).
  British prototype, BR-era turnout geometry. High confidence.
  - The author's documentation survives in the Wayback Machine ("UKFS
    Documentation",
    <https://web.archive.org/web/20061031232958/http://ukfs.trainsimfiles.com:80/docs/>,
    with pages on straights, curves, turnouts, crossings, third and fourth
    rail; captured 2007). The site moved from
    `www.trainsimfiles.com/products/ukfs/` (2003-2005) to
    `ukfs.trainsimfiles.com` (2006-2007); the domain has been parked since
    2009.
  - v3.1.6, released on 20 March 2005, is the last version; it needs
    tsection build 29 or later. Packages today: trainsim.com fid 11889
    (45,929 downloads), an installer (fid 88469, 2023) and the four texture
    sets of February 2006 (fids 18021, 18023, 18025, 18029). Steam4me's
    mirror has the installer without textures
    (<https://nekocoaster.github.io/steam4me1/routes/files/UKFS_v3_1_6_full.exe>);
    the Wayback Machine has the author's downloads of 2004-2007. Community
    fixes: UKFS-Fix v2.2 (2018), UKFS_Install (2019), UKFS-Fix3 (October
    2025, fid 84414; seen in search text only).
  - Pictures: `/home/arch/tsection-families-web/pictures/ukfs/` (11, from
    the author's pages: the four texture sets, rails and chairs against
    default track, double track at the six-foot way, turnout and crossing
    detail, third-rail track).
- Shapes: 1,115 / 0. None local. PRUDNIK: 0.
- Kind and gauge: track, 1.435 m written as 1.435 (Kraus-Yao's 65 at 1.5).
  Two-track spacing by name: 3.413 m (±1.7065, the British "six-foot"
  way), `_10` 4.632 m (10 ft way), `_12` 5.242 m (12 ft way), Kraus-Yao's
  `_11a`/`_11b`/`_11c` 4.985 m (Kuju).
- Contents: single 460, parallel 253, junction 220, diamond 76, frog piece
  22, tunnel 83, special 1. Names `ukfs[3r|4r]_<type>_...`:
  - `s` straights, `c` curves, `is`/`ic`/`ictxc`/`icac` interworking
    straights and curves; third-rail curves `ukfs3r_cl_`/`ukfs3r_cr_` with
    the conductor rail on the left (inside of the curve) or right
    (outside), multi-track ones named by the inside radius; third-rail
    interworking curves exist only with the rail on the left (UKFS docs);
  - check-railed curves end in `_cr`: `ukfs_c_<paths>x<radius>_<angle>_cr`,
    "curves up to around 200m (10 chains)" (UKFS docs; 24 names in
    Build 54);
  - turnouts by crossing angle, 11.42° (1 in 5) to 2.86° (1 in 20) (1 in
    10 = 5d71, 1 in 8 = 7d13, 1 in 7 = 8d13, 1 in 6 = 9d46, 4d76, 4d4; `d`
    stands for the decimal point in angles, `m` in lengths and radii):
    `tl`/`tr`/`ty` and manual `tlm`/`trm`/`tym` ("fitted with a realistic
    lever"), short `tys`, curved `tcl`/`tcr`, three-way `t3i`, scissors
    `tsc` and curved `tscc`, catch points `*_cat_*`, single and double slips
    (`xss*`, `xsd*`, `tsl*`, `tsr*`, curved `tcs*`, `xcs*`), diamonds `x`,
    `xl`/`xr` (to 90°), external slips `xes*`;
  - slips and some junctions are built from overlapping shapes (UKFS docs):
    slip shapes `ukfs_xs*` take drop-in switch modules `ukfs_tsls*`/`tslsm*`
    (older `xssl2`-style slips take `tsl`/`tslm`), curved slips `tcsli*`,
    external slips `xes*`; three-way `t3i*` and scissors `tsc*`/`tscc*` take
    turnout switch modules `tswl*`/`tswlm*`/`tswcl*`. A UKFS slip in a
    route is two or more shapes on top of each other;
  - "geometry" pieces `g*` (turnout ends and extensions at 6, 10, 12 ft
    ways), turntables `tt` (42, 65, 68 ft) with connectors `ttcon_1m`/`2m`/
    `3m`, wagon turntable, buffers `bufr` (rail frame, sleeper beam),
    `bufr2` (painted targets), `bufr3` (rail buffer beam);
  - other groups (UKFS docs): `ukfs_srg` rusty, grassy track for disused
    sidings, `ukfs_sro` rails without trackbed ("for custom bridges"),
    `ukfs_si` inspection pits, `ukfs_stuno` a double-track tunnel with one
    path offset to one side, `_10a` and similar multi-track pieces with
    mixed ways (`_10`/`_12` came with V3.1.5);
  - tunnels `stun`, `ctun`, `stuno`; 3rd rail `ukfs3r_*` with tunnels and
    ramps `ukfs3r_sr[l|t]_<paths>x<length>_[l|r|lcr|rcl]` (`l` leading, `t`
    trailing; `lcr` = ramp left, continuous rail right) and changeover
    ramps `ukfs3r_srco_*_l` (left to right) and `_r`; 4th rail `ukfs4r_*`,
    "based on London Underground 4th rail specifications for surface
    rails" (a subterranean 4th rail "will not be developed"); `_INFO`
    "2x200m radius checkrailed curves".
- Look: not local; from the UKFS docs and the pictures. Rails "built to the
  correct size", raised "as if supported by railchairs" (the chairs are 2D,
  by transparency); a "profiled trackbed which also avoids track textures
  clashing between overlapping shapes"; sleepers 8 ft 6 in x 10 in (12 in
  in turnouts) at 0.762 m (30 in); turnouts and crossings with "correct
  switch blades, checkrails, and frog/vee". One geometry, four looks chosen
  per route by its textures (installed into each route's TEXTURES; the
  shapes are global): 1 wood sleepers, brown ballast, jointed rail with
  chairs ("steam era"); 2 the same on grey ballast; 3 concrete sleepers,
  brown ballast, welded rail "with rail-clips" ("modern era"); 4 the same
  on grey ballast. The pages do not name the rail section; chairs suggest
  bullhead and clips flat-bottom rail (the picture search's inference).
- Profile needs:
  - a 1.435 m family with side roles for 3.413, 4.632, 5.242 and 4.985 m
    spacing (`ukfs_*_10`, `_12`, `_11?` filters);
  - one look per route by its texture set (1-4), not by shape name;
  - conductor rails: `ukfs3r_*` third rail (side from `cl`/`cr`, `l`/`r`),
    `ukfs4r_*` third plus centre fourth rail, with ramps;
  - check rails on curves, by the `_cr` suffix (`ukfs_c_*_cr`);
  - `COMPLEX` for many slip and curved-turnout forms; catch points; a slip
    and its switch modules are separate overlapping shapes that must draw
    as one group;
  - tunnels: tube polylines (the tube profile is probably in the archived
    `ukfs_tunnel_diagrams.pdf`, not read:
    <https://web.archive.org/web/20040313122516/http://www.trainsimfiles.com:80/products/ukfs/download/ukfs_tunnel_diagrams.pdf>);
    turntables `DISABLED`.

#### BR Rail Project (Joseph Pearson)

- Source: "Shapes from Joseph Pearson's BR Rail Project" (`_INFO`,
  allocation "Joseph Pearson" 15000-16799). British (BR) by name and by the
  turnout angles. Medium confidence.
  - Package: "BR_Project_Complete" (fid 22268, "Buckeye (Joseph Pearson)",
    2009-10-15, 44.11 MB): "The complete beta version of the BR_Project".
    Its 23 inner zips (2009-10-11): curves 100-7000 m, blades, buffer stops,
    diamond crossings, IC curves, IS straights, points, three-way points,
    point extensions, straights, double straights, textures (13.4 MB) and a
    tsection. The parts were also uploaded alone (fids 22273 to 22383,
    October 2009). Each part holds `BR_<part>.exe`, `BR_Rail_Project.doc`
    and `tsection.zip`; the installers and the `.doc` were not read.
  - The readme in "BR_Buffer_Stops" (UKTS 22267,
    <https://web.archive.org/web/20230115172658/https://www.uktrainsim.com/filelib-info.php?form_fileid=22267>):
    the set was meant to "complement Tim Booth's UKFS track system", with
    "83MB of track shapes available to download"; it needs tsection 40 or
    later.
  - Use: the Chiltern Route BETA (David J. Forrest, Open Rails, 2020; its
    file 02, fid 39521) ships about 170 `BR_` shapes (curves, straights,
    double straights, interworking pieces, turnouts, buffers) dated 2009.
  - Pictures: `/home/arch/tsection-families-web/pictures/br-rail/` (2,
    trainsim.com's untextured renders of `BR_s_100_w01a` and `_w01b`).
- Shapes: 1,281 / 0. None local. PRUDNIK: 0.
- Kind and gauge: track, 1.435 m (1.435). Two-track `BR_s2_` at 3.413 m.
- Contents: single 960 (`c` curves, `s` straights, `ic`/`is` interworking
  curves/straights, `ccr` curves), parallel 64 (`BR_s2_`), junction 130
  (`tl`/`tr`/`t`/`ty`, `rb`/`lb`, turnout extensions `tl_ext`/`tr_ext`;
  angles 5d71, 7d13, 8d13, 9d46, 11d42), diamond 70 (`xl`/`xr`/`x`, to
  14d26), frog piece 4, buffers 53 (`BR_Buffer_01` ... `_53`).
- Look: not local. Each geometry exists in several looks by suffix after
  `01` / `w01`: `a`, `b`, `g`, `bw`, `gw`, `db`, `dbw`, `w`, `ww` (e.g.
  `BR_c_7000_01_w01b`). Reading them as ballast colour (b brown, g grey) and
  sleeper or wear variants is a guess (unconfirmed). The Chiltern route
  ships the 100 m straight as `_w01a`, `_w01b`, `_w01g`, `_w01gw`, `_w01w`
  and `_01a`. Their sizes differ: `w01a` and `w01gw` 79 KB, `w01b`, `w01g`
  and `w01w` 65 KB, so `a` and `gw` carry more geometry than `b`, `g` and
  `w`. What that geometry is was not found; the untextured renders look the
  same.
- Profile needs: like UK FineScale without conductor rails: one family per
  suffix look (filters `BR_*_w01b`, `BR_*_w01g`, ...), side roles at
  3.413 m, `COMPLEX` for turnouts and diamonds, a buffer mesh later (53
  types).

#### MTracks (Mark Hyams)

- Source: Mark Hyams, MTracks ("Mark Hyams Mtracks sections", allocation
  "Mark Hyams MTracks"). Build 38 readme: "MTracks V1.9, Download at:
  http://home.comcast.net/~markhyams/mtracks". XTracks credits: "Mark
  Hyams - smother [smoother] curves". PSK: "Mtracks 1.902PL" with Polish concrete
  additions. 12 shapes by Scott Benson, 18 in Hans van Oost's range. North
  American practice by name: curves named by degree of curvature, frog
  numbers, 14 ft track centres. High confidence for the author; the US
  routes below confirm the North American use.
  - "M Tracks means Mainline Tracks" (<https://digital-rails.com/mtracks/index.html>,
    a mirror of the Comcast site, with versions to 1.50 and `mtracks.zip`).
    v1.90 is trainsim.com fid 66117 (2005-11-17). eric's "Replacement
    MTracks" with Kuju/ATracks textures are fids 88502 (2024) and 88569.
  - Use: "most routes by Jerry Sullivan" (Elvas Tower 2025,
    <https://www.elvastower.com/forums/index.php?/topic/38752-rmd-east-12/>):
    L&N/CSX Knoxville Sub (fids 86064, 88600: "Requires Mtracks"), Southern
    Railway CNO&TP 3rd District (fid 87134: "Mtracks is included"); also the
    Milwaukee Road Aberdeen Division (fid 84979). In Poland CMK (about 8,150
    objects with reskins) and E30 (1,000+), often as reskins in route-local
    folders (`track_b/nasypy*`, embankment variants).
  - Pictures: `/home/arch/tsection-families-web/pictures/mtracks/` (2).
- Shapes: 445 / 569. Local 534 of 614 (OR_PSK_NEW, tsdata). PRUDNIK: 97
  objects on M indices (63 `DB/DB2he_M2tC*_emb_z`, 24 `m2tc0007d0020r.s`).
- Kind and gauge: track, 1.435 m (1.5).
- Contents: `M<n>tC<DDMM>d<rrrr>r`: curves named by US degree of curve in
  degrees and minutes and the angle in radians (`M1tC0230d0200r`: 2°30',
  R 698.61 m, 0.200 rad); 1 and 2 tracks; two-track at 4.27 m (14 ft),
  `M_25f_` at 7.62 m (25 ft); `TransKtoM` transitions from Kuju spacing;
  turnouts `PntNo15/20/24` (frog numbers), `FrogNo*`, `EndPntNo*`,
  `XoverNo*` crossovers with `Kuju` variants; `M_Tun_` tunnels 3°-12°
  (Build 54 only); PSK adds `Concrete` copies of most pieces and `LC`
  pieces.
- Look (looked at `M1tC0230d0200r`, `M2t10mStrt`, `M1tPntNo15Lft`,
  `M2tC0010d0010rConcrete`, `M2t500mStrtLC`, `DB/DB2he_M2tC0035d0010r_emb_z`):
  Kuju textures (`ACleanTrack1/2`, `Concrete1`), bed at y 0.20, rails to
  0.32; PSK's `LC` uses Polish textures `acleantrack-podsypkaL-64`.
- Profile needs: `MAIN` and side roles for 4.27 m and 7.62 m spacing
  (paths are not symmetric: the first path at x 0, the second at 4.27);
  `COMPLEX` for the frog-number turnouts and crossovers; filters `M*Concrete*`
  and `M*` with the A look; `DB2he_*`: see the A entry.

#### Bjorn van Steenbergen (DR_)

- Source: "Track/Road Sections from Bjorn van Steenbergen" (`_INFO`,
  allocation 27300-28099 "Bjorn van Steenbergen Road/Track"). The road
  names are Dutch (`Afslag` exit, `Oversteek` crossing, `Voorsorteer`
  turning lane, `Tankstation`), so a Dutch set is likely. His trainsim.com
  uploads, as "dutchrail (Bjorn van Steenbergen)", are Dutch rolling stock
  of the "European Corridor" (`EC_*` files; fids 11769, 17138, 17219,
  17273, 64353; 2005-2007). So `DR` probably stands for "dutchrail" (the
  trainsim.com round's reading; no page says so). No track package, route
  or picture was found.
- Shapes: 797 / 797 (703 track, 94 road `DR_Rd_Type`). None local. PRUDNIK: 0.
- Kind and gauge: track and road; 1.435 m (1.5).
- Contents (track): six copies of one geometry set, one per letter `t`,
  `n`, `r`, `y`, `s`, `h` (`DR_tA...` to `DR_hA...`): 1-4 track straights
  `s` and curves `b` (spacing 5.0 m exactly: paths at 0, -5, -10, -15),
  and on single track `r`: diamonds `cr` (5°, 10°), switches `sw`,
  crossing switches `cs` with `SW`/`FS`/`XO` pieces, `end`, `diagonal`.
  Groups: single 199, parallel 396, junction 54, diamond 54.
- Road prefix (road section): `DR_Rd_Type1-4` straights, curves, T,
  crossings, exits, fuel station (94, `RoadShape`).
- Look: not local; the six letters are six looks (meaning unconfirmed).
- Profile needs: six track families chosen by `DR_<letter>A*` filters,
  side roles at 5.0 m (not 4.985), `COMPLEX` for `sw`, `cs`, `cr`; ROAD
  members for `DR_Rd_Type*`.

#### Europeanbahn

- Source: Europeanbahn, a commercial UK developer (developer list:
  http://www.europeanbahn.co.uk/; Build 38 readme: "Europeanbahn (Commercial
  Add-Ons)"). The 4th-rail pieces suggest London Underground or similar
  routes (unconfirmed which product). Allocation 34000-34999.
  - Products, from the archived site: London Brighton Express (2002; it
    "installs tsection.dat build #19",
    <https://www.nklj.se/xtracks/documents/routes.htm>) with an Add-pack,
    West Coastway (2004), East Coast Express (King's Cross-Peterborough,
    2004-2007, with ECE2 to ECE4), West Coast Main Line (Euston-Birmingham,
    2006), London South Coast, Cross City (2008), Midland Main Line (2008)
    and the Farringdon Extension (West Hampstead-Farringdon-Moorgate or
    Blackfriars, 2008-09, with an underground section at Farringdon). The
    shop sold downloads until about 2012
    (<https://web.archive.org/web/20110418211207/http://www.europeanbahn.co.uk/shoppingcart/sh_Main.htm>);
    its pages redirect from mid-2013, and the domain is gone. No separate
    track package was found: the shapes came with the products.
    trainsim.com has only route patches (fids 24558, 24553, 2010).
  - All 437 entries are already in Build 38 (November 2007). The 293 found
    locally have indices 34000-34490; the 140 not found (`TunF65`/`TunFW65`
    tunnels, `_vtun`, `3r`/`4r`, `10ds` points, `EB_*WireEnd_LG` at
    34500-34503) are one later block, 34491-34656. Being in Build 38, they
    predate the Midland Main Line and Farringdon releases; which product
    they were made for is still unknown.
  - Pictures: `/home/arch/tsection-families-web/pictures/europeanbahn/`
    (10, Europeanbahn's screenshots; which shapes they show cannot be
    told). Picture 05 (London Brighton Express, 2004) shows a conductor
    rail between the tracks, where the `i` pieces put it.
- Shapes: 437 / 0. Local 293 of 433 in OR_PSK_NEW and tsdata (as `EB_*`),
  although PSK's tsection has no `eb_` entries. PRUDNIK: 0.
- Kind and gauge: track; 1.435 m (1.5); spacing 4.985 m.
- Contents: single 54, parallel 177, junction 29, diamond 9, tunnel 168.
  1-4 track straights and curves 250-2000 m; `i`/`o` copies of multi-track
  pieces ("Alternating sides Inside/Outside": the conductor rail between or
  outside the tracks); `3r`/`4r` third and fourth rail pieces ("4th rail
  electrified Sections, Above Ground"); points 2.5°-10°, short `10ds`
  points, slips `Slp`, `SglSlip10d`, Y, crossings to 22.5°, 45° yard,
  skew pieces; tunnels `Tun`, `RndTun`, `TunF65` ("Flat Topped 6.5 Metre
  Height", "Tunnel NOT Wired") and `TunFW65` ("Wired Tunnel"), `Tuni`/`Tuno`,
  `_vtun` virtual tunnels; `EB_1tWireEnd_LG` overhead wire end sections with
  a lattice gantry for 1-4 tracks; `eb_A*` two tunnel switches.
- Look (looked at `EB_1t500r5d`, `EB_2t500r5di`/`o`, `EB_2t10mstrti`,
  `EB_2t500r10dTun`, `EB_2t500r10dTuni`, `EB_1t2000r5dRndTun`,
  `EB_2t10mstrto`): the local copies use Kuju textures (`ACleanTrack1/2`,
  `ATrackTunWall`, `ATrackTunFloor`; possibly PSK's re-texturing,
  unconfirmed); rails to y 0.35; `o` pieces have a conductor rail 1.06-1.18 m
  outside each track centre; two-track tunnel walls to 10.1 m, round
  tunnels to 6.3 m.
- Profile needs: conductor rail by side (`*i`, `*o`, `3r`, `4r`); tunnel
  families: Kuju-like square, round, flat-topped 6.5 m, the same with a
  contact wire (`FW65`); `_vtun` track only; `COMPLEX` for points, slips,
  crossings; `EB_*WireEnd*` static (`DISABLED`).

#### Via Metrica / Sierra Menera

- Source: Jose Sancenón, "Sierra Menera Metric Track" (developer list:
  "Jose Sancenón (Sierra Menera Metric Track)"; allocation "Jose Sancenon
  Metric"). Steam4me lists it as Via Metrica by "Sierra", "a lapsed MSTS
  user on trensim.com", "a range of meter-gauge track", version 2 with
  tsection build 44. The name points to the Sierra Menera mining railway
  (Ojos Negros to Sagunto, Spain); that the set models it is unconfirmed.
  `VD` is "Sierra Menera 500 mm Track". One shape by Jeffrey Kraus-Yao.
  - Steam4me calls its installer "a Qsetup installer ... written by Yuri Sos
    with permission from designer 'Sierra'"; the zip installs
    `GLOBAL\SHAPES` and a `GLOBAL\tsection.dat`. Its three downloads answer
    404, and the sizes on that page are XTracks 3.20's, copied from the
    XTracks page. trensim.com (download id 1059) still served the set in
    November 2024 (trainsim.com thread 2314351); in November 2026 it was
    offline after a security problem.
  - Use: Ferrovia delle Dolomiti (Italy, 2011, re-uploaded 2024) bundles
    it ("binari via metrica per il corretto funzionamento dello scenario";
    <https://www.trainsimhobby.net/infusions/pro_download_panel/download.php?did=923>);
    La Mure (France, 2007, fid 60984; thread 150708 lists a missing
    `vm1t200mstrtrtun.s`); the Argentine Trasandino route (FCGB C14, Open
    Rails, November 2024; thread 2314351 asks for VMTracks). A TSH moderator
    (October 2025) names "via metrica" among the sets users download. No
    Spanish route was confirmed. "VM tracks are not indentical too M
    Tracks" (thread 99345).
  - Pictures: `/home/arch/tsection-families-web/pictures/via-metrica/` (1,
    the La Mure thumbnail, low confidence).
- Shapes: 400 / 0 (`VM` 380, `VD` 20). None local. PRUDNIK: 0.
- Kind and gauge: track; `VM` 1.0 m (1.0), `VD` 0.5 m (0.5).
- Contents: single 197, parallel 76, junction 32, diamond 9, frog piece 2,
  tunnel 75 (`Rtun`), bridge 2 (`Brdg`), special 7 (buffers, dummy). Look
  suffixes `C`, `Ox`/`ox`, `Sox` on the same geometry; points by radius
  (`PNT90`, `PNT150`, `PNT250`, curved `PNT150c90`, `Y`), 90° diamonds
  `X90d`, a crossing with standard track `VM1tA1tX90d`, `Placa20` (Spanish
  "placa giratoria", turntable, by name).
- Look: not local. `C` may be concrete and `Ox` oxidised (rusty); both
  unconfirmed.
- Profile needs: a metre-gauge family (rails at ±0.5 m), looks by suffix
  filters (`VM*C.s`, `VM*Ox*`), round tunnel tubes, `COMPLEX` for points
  and 90° diamonds, a 500 mm family for `VD`, the dual-gauge crossing
  `VM1tA1tX90d`, `Placa*` `DISABLED`.

#### Tram track (Ralf Riemer)

- Source: Ralf Riemer, "Tram Track Shapes from Ralf Riemer Tram Track"
  (allocation "Ralf Riemer Tram"). German by the texture name `Bordstein`
  (kerb). Released in 2003 on msts-strabafanpage.de
  (<http://web.archive.org/web/20080131101722/http://www.msts-strabafanpage.de:80/Page/include.php?path=news&catid=0&entries=65&PHPKITSID=GEqTF08bPhALi5N4q5ejA8d0qfRMZOc1>);
  a v1.1 is mentioned on trainsim.com (search snippet only). The original
  host is parked; the set ships in PSK's GLOBAL. Use: PRUDNIK, CMK (55
  `TramBB_A`); the Chiltern Route BETA (fid 39521) ships `TramB_A` shapes.
  No German route was found.
- Shapes: 92 / 92. Local 89 of 92 (OR_PSK_NEW; 18 in tsdata). PRUDNIK: 488
  objects (`tramb_a2t12_5mstrt.s` 136, `tram_a2t12_5mstrt.s` 82, ...).
- Kind and gauge: track laid in streets; 1.435 m (1.5).
- Contents: single 40, parallel 22, junction 12, diamond 15, frog piece 2,
  tunnel 1. Four looks: `Tram_A` (in pavement), `TramB_A` (ballast),
  `TramBB_A` (with a kerb), `TramZ_A` (rails only, one shape). Straights
  0.5-100 m, curves 25-100 m, points `25PntLft/Rgt`, diamonds `Xover25`,
  `Xover90`, crossover ends.
- Look (looked at `Tram_A1t25r5d`, `Tram_A1t25PntLft`, `TramB_A1t0_5mstrt`,
  `TramBB_A1t12_5mstrt`, `TramZ_A1t12_5mstrt`, `TramB_A2t12_5mstrtTun`):
  `Tram_A` has a 3.1 m paving surface (`acleanTrack1T`) at y 0.32 flush
  with the rail tops (`ACleanTrack2T`, 0.32-0.34). `TramB_A` a 3 m
  wood-tie-on-ballast bed (`acleanTrack3`) at 0.20. `TramBB_A` has its rail
  vertices at y -0.14..0.01 (perhaps raised by a matrix; not checked), the
  bed at 0.20 and a kerb (`Bordstein`) from 1.42 to 4.72 m on the right.
  `TramB_A2t12_5mstrtTun` has the tunnel flag and no tube.
- Profile needs: a TRACK family whose bed polyline is street paving at rail
  height (no ties), grooved rail (`Rail` mesh); a ballasted tram family; the
  kerb as a polyline; `COMPLEX` without a `Tie` item for tram points and
  diamonds (the paving stretched as bed); side roles at the set's 3.0 m
  spacing. Road-and-rail joins are not covered.

#### Y-Tracks (Juergen Romanowski)

- Source: Juergen Romanowski (YTracks) (developer list, allocation 31300-
  31399, "Romans Y-Track Shapes"). Build 38 readme: "YTracks V1, Download
  at: www.train-sim.com". "MSTS YTracks" on trainsim.com (fid 61734,
  2004-04-04): "switches ... as German prototype aka 'Einheitsweichen'",
  "Includes Tsection.dat V23"; "YTracks 02" (fid 69398, 2007-07-09) needs
  tsection build 33. PSK: "Ytracks 1.81PL" (v1.81 PL, 2005, from the
  TS-Studio site, gone:
  <https://web.archive.org/web/20090209163017/http://ts-studio.eu07.pl:80/down-tracks.html>).
  A readme on trensim
  describes "a set of track pieces built around 1:9 turnouts, including a
  190-1:9 family" (<https://ftp.trensim.com/foro/download/file.php?id=13718>,
  search snippet only; the file did not open). German DB turnout
  geometry (radius 190 m, 1:9), confirmed by the package text. High
  confidence.
- Use: in E30-Podkarpacie Y shapes are the most common track
  (`y1thz190-1-9lft` 826, `y1tpnt190rgt` 605, ...); the usage round notes
  that the 1:9 turnout is the standard Polish one. Stockholm Regional (fid
  28001, 2011) ships `Y1tHz` frogs. Pictures:
  `/home/arch/tsection-families-web/pictures/ytracks/` (3).
- Shapes: 66 / 86. Local 84 of 86. PRUDNIK: 220 (`y1tendpnt190-1-9` 46,
  `y1thz190-1-9rgt/lft` 45, `y1tpnt190*mnl` 30, ...).
- Kind and gauge: track; 1.435 m (1.5).
- Contents: junction 26, diamond 15, frog piece 2, single 23 (PSK adds 20,
  three `Concrete`). `Pnt190`, three-way `3Pnt190`, `Hz` (Herzstück, frog
  piece) 1:9 and 1:7.5, `Sc` scissors, `V` pieces, `YPnt`, single and
  double slips `SglSlip190`/`DblSlip190` with `Xover` centres, `Fit`
  fitting pieces, `EndPnt`.
- Look (looked at `Y1tPnt190LftMnl`, `Y1tHz190-1-9Lft`,
  `Y1t190-1-9_12mStrtConcrete`, `y1tDblSlip190LftMnl`): Kuju-style flat bed
  (`Ycleantrack1`, brown wooden ties) at y 0.19-0.20, rails to 0.33-0.34;
  `Concrete` uses `Ycleantrack5` (concrete ties).
- Profile needs: `COMPLEX` for the 1:9 points, frog pieces, scissors and
  slips (the slips come as pieces, like A's: `ExcludedParts`/`IncludedTrackShape` filters);
  filters `Y*Concrete*`, `Y*`, `y*`.

#### XTracks subterranean S (with STracks and MÁV pieces)

- Source: XTracks: "Subterrainian sections (tunnels without walls) and
  scale switches" (versions page, v3.4); "Paul Andrews - got the
  subterrainian sections added"; Norbert Rieger "initiated new subterrain
  sections" and tunnel switches. Also in `S`: Paul Andrews' STracks tight
  curves ("for normal use "s" sections"), 17 MÁV switches by Szilárd Futó
  (`S1tPntMAV*`, Build 54 only), Steven Masters and R. Fiore pieces. High
  confidence.
- Shapes: 143 / 121. Local 85 of 143. PRUDNIK: 72 on S indices; 70 of them
  carry DBTracks names (`DB/DB30_A1t100mStrt.s` ...), 2 are `S2t250mStrt.s`.
  CMK: 303 `S1t*` objects (`S1t1mStrt` 111). Ships in every XTracks
  package.
- Kind and gauge: track with `TunnelShape` (120) and no tube; 1.435 m (1.5).
- Contents: the A geometry again (straights, curves, 3°/4.7°/6° points,
  frogs, crossings) flagged as tunnel; MÁV switches and STracks curves are
  not flagged.
- Look (looked at `S1t250mStrt`, `S1t500r5d`, `S1tPnt4_7dLftMnl`,
  `S2t250mStrt`): Kuju-style 5 m bed; in OR_PSK_NEW the bed texture
  `SCleanTrack1` shows concrete ties on light ballast. No walls.
- Profile needs: track profiles only. `Kind ( Tunnel )` must not give these
  a tube: a filter `S*` (and the DBTracks names on S indices) before any
  tunnel filter, or a kind that tests for walls.

#### Italian track (AlexB)

- Source: "Binari italiani con massicciata e interbinario di 2.12 m. -
  AlexB" (`_INFO`, Italian). Indices in Renzo Grassi's range (37100-37299;
  developer list: "Renzo Grassi http://www.ildeposito.net/"). Italian FS
  practice: turnouts "S60UNI/170/0.12 Vmax. 30 km/h", "S60UNI/250/0.12",
  "S60UNI/400/0.094 Vmax. 60 km/h", "S60UNI/1200/0.040 Vmax. 100 km/h",
  curved turnouts `CI`/`CE` (inner/outer).
  - ilDeposito publishes the set with the `VT` track in "VT-Tracks e
    IT-Tracks" (<https://www.ildeposito.net/NuovePagine/xtracks.html>):
    v1.0 of 11/12/2017, v1.1 of 5/04/2019 ("modificate piattaforme da 16 e
    21 m"), authors "OBY, Renzo Grassi, Alessandro Baldi", 5.30 MB,
    "Contiene la versione 52 del Tsection", "necessari per gli scenari
    realizzati dal 2009 in poi". The zip is live
    (<https://www.ildeposito.net/Files/Vari/VT_Tracks-IT_Tracks.zip>,
    2025) and on TSH. AlexB is very probably Alessandro Baldi (the tsection
    credits AlexB, the package Alessandro Baldi; the picture search's
    inference). The package's 16 m and 21 m platforms are not among Build
    54's `It` names.
  - A sibling set, "BINARI ITALIAN TRACK V1.0" (August 2006; `Ca1t*` to
    `Ca4t*`, track with built-in catenary, 114 pieces, by "Renzo428" and
    "AlexB"; not in the global tsection), has a project page that lists the
    turnouts S.60/250/0.12, S.60/400/0.074 and S.60/1200/0.055, the same
    family as the `It` points (<https://www.ildeposito.net/NuovePagine/ittk/ittk.htm>).
    Its two pictures (grey ballast, light concrete sleepers, masts and
    wire) are in `/home/arch/tsection-families-web/pictures/italian-alexb/`;
    no picture of the `It` shapes was found.
  - Use: no released route was shown to place `It` shapes, and no
    trainsim.com zip holds one. Guido Ferretti (2017): "Non sono VT ma it e
    sono usati nella mai finita faentina"; the SILA route (Open Rails, 2023,
    fid 88325) asks for the IT-VT package.
- Shapes: 200 / 0. None local. PRUDNIK: 0.
- Kind and gauge: track; 1.435 m (1.5). Spacing 3.555 m (paths ±1.7775;
  2.12 m between inner rails); "adattamento interbinario It/Default"
  pieces join Kuju spacing.
- Contents: single 52, parallel 64 (1-4 tracks; radii 300-2000 m), junction
  24, diamond 8 (slips `DblSlipU`, `SglSlipU`, `XoverU`), tunnel 52 (`Sub`,
  "sotterranei", underground).
- Look: not local; ballasted ("con massicciata"), UIC 60 rail by the
  turnout names.
- Profile needs: a 60E1 family with side roles at 3.555 m; `COMPLEX` for
  S60UNI points and slips; `Sub` tunnels (walls unknown: check before
  giving a tube).

#### Rack rail (Simon Merjan, Clemens Hanel)

- Source: "Rack Rail shapes from Simon Merjan, textures by Clemens Hanel"
  and "Metre Gauge shapes from Clemens Hanel and Simon Merjan" (`_INFO`;
  allocation "Clemens Hanel Z-Track"). The developer list gives
  http://www.hcl.li/schneeberg/schienenmeteren.html (not reachable), which
  points to the Schneeberg rack railway in Austria (metre gauge, Abt rack:
  <https://en.wikipedia.org/wiki/Schneeberg_Railway_(cog_railway)>); that
  the set models it is unconfirmed. `Z` is German Zahnstange (rack).
  - Packages: "Z-Tracks and MT-Tracks Standard Edition" on trainsim.com,
    `ztracks.rar` (fid 64691, 2005-04-13) and `ztracks.zip` (fid 73586,
    2009), and a Route Builders' Edition (fid 64690). Readme: "Clemens
    Hanel, Vienna, 12th of April 2005"; needs "X-Tracks 3.13 or
    tsection.dat Version 00029". An earlier `ztracks.rar` was offered on
    hcl.li in November 2004, needing tsection 00030 (the Wayback Machine has
    the pages, not the file:
    <http://web.archive.org/web/20101119033939/http://www.hcl.li:80/schneeberg/downloads.html>).
    Neither the Schneebergbahn route nor a Dampfbahn Furka project (2006-07)
    was released (<https://www.mstsforum.info/index.php?topic=1443.0>).
  - Use: the Snowdon Mountain Railway route (UKTS, 2013, fid 32042): "The
    Z-Tracks add-on track system is required". Guido Ferretti's Open Rails
    routes ship `MT1t*` tunnel pieces and `gf_` reskins: Trento-Malè-Marilleva
    (fid 88232, 2023), SILA (fid 88325, 2023), Fiemme Valley 1960 (fid
    88703, 2024). Not checked: the track of the Italian rack routes
    Diakofto-Kalavrita (2011) and Rittnerbahn (2015).
- Shapes: 199 / 199. All local (OR_PSK_NEW; the `Z*` textures are missing).
  PRUDNIK: 0.
- Kind and gauge: track; 1.0 m (1.0).
- Contents: `MT` metre-gauge track without rack (66: curves 36-500 m,
  20° points, slip pieces 14°, crossings, tunnels; `MT1tRT5mstrt` has
  rack parts, perhaps a rack entry, unconfirmed);
  `Z`, `ZG` ("Grass rack tracks"), `ZH` ("Half Grass"), `ZI` ("Inside
  Grass") with rack: straights, curves from 36 m radius, 20° points, Y,
  buffers, tunnels.
- Look (looked at `MT1t20mStrt`, `MT1t500r5d`, `MT1t5mStrtTun`,
  `MT1tRT5mstrt`, `Z1t20mStrt`, `ZG1t36r1d`, `ZH1t0_5mStrt`,
  `ZI1t0_5mStrt`): bed 2.5 m wide at y 0.20; `MT` rails to 0.30; `Z*` rack
  in 3D up to 0.37 (4,288 triangles in 20 m); `ZG`/`ZH`/`ZI` differ only
  in the bed texture. trainsim.com's untextured render of the package's
  `Z1t20mStrt` (`/home/arch/tsection-families-web/pictures/rack-rail/`)
  shows only a narrow toothed strip, which the trainsim.com round read as a
  rack-only piece laid over `MT` track (unconfirmed). The local copy says
  otherwise (checked 2026-11-27): a 2.5 m bed (`ZCleanTrack1`, 2
  triangles) and 4,288 triangles of rails and rack within x ±0.51 m
  (`ZCleanTrack2`), so a `Z` piece is complete track.
- Profile needs: metre-gauge families; a rack rail part (a `Sweep` or
  `Repeat` of rack teeth in the centre); grass beds by filter; in rack
  points the rack follows which path is not covered by `COMPLEX`; tunnels.

#### XTracks narrow gauge (N)

- Source: Okrasa Ghia, XTracks narrow gauge ("Okrasa Ghia Narrow Gauge"),
  with curves by Doug Jones, DualTracks helpers by William Adams,
  diverging switches by Jared Singletary, and `NA1tXover6d*` "Xover
  between different gauges" (XTracks versions). `I1t20mStrt` is "XTracks
  invisible track". High confidence.
- Shapes: 173 / 168 (`N` 170/165, `NA` 2/2, `I` 1/1). Local 168 of 192.
  PRUDNIK: 0. E30-Podkarpacie places 144 `N1t*` pieces, probably as a
  stand-in for the 750 mm Bieszczady forest railway (the usage round's
  guess; not inspected); CMK 12. Steam4me's VR 2 ft 6 in Gembrook-Cockatoo
  route needs only XTracks, so it probably uses them (unconfirmed;
  <https://nekocoaster.github.io/steam4me/routes/vic_pb_1920.html>).
- Kind and gauge: track; 3 ft, 0.861 m (0.9); spacing 2.991 m (XTracks
  "Track info").
- Contents: single 80, parallel 9, junction 56 (10° points, 3° modular,
  stubs, Y, 45° yard, 30° tram, 0° points, LC/RC dual-gauge points),
  diamond 12, tunnel 10, special 6 (buffer, derail, dummy, turntables
  10 m and 16 m). PSK adds 19 `*Tun` pieces under "tun_ghoost".
- Look (looked at `N1tPnt10dLft`, `N1t300r5dRndTun`, `N1t15mStrtTun`,
  `I1t20mStrt`, `NA1tXover6dLft`): 3 m bed at y 0.20, wooden ties on sandy
  ballast (`ncleantrack1`), rails to 0.32; round tunnels to 5.1 m; the PSK
  `N1t15mStrtTun` has the tunnel flag, a floor and no walls;
  `ICleanTrack1` is fully transparent.
- Profile needs: a 0.861 m family with side roles at 2.991 m; `COMPLEX`;
  round tunnel tubes; PSK ghost tunnels track only; `I*` an empty profile
  or `DISABLED`; `NA*` crossings between gauges need two gauges in one
  `COMPLEX` group.

#### HSR slab track and tunnels (Norbert Rieger, V4hs)

- Source: "Track Shapes from Norbert Rieger, HSR Slab Track" and "HSR
  tunnel" (`_INFO`; allocation "Norbert Rieger" 17680-17919). The DBTracks
  Open Rails profiles match "the `DB` and `V4hs` prefixes" and list `V4hs`
  among the DBTracks variants (<https://github.com/pgroenbaek/dbtracks-stf-profiles>),
  so it belongs to the DBTracks family. German high-speed line. Medium
  confidence.
  - The profiles' README lists a DBTracks "V4hs" set with the variants
    `DB1`, `NP1`, `NP2`, `R2k` and `RKL`; the global tsection has only `R2k`
    and `NP1`. The profiles "mimic the original DBTracks shapes as best as
    possible": `NP1`/`NP2` a slab with no sleeper part and no LZB cable,
    `R2k` a slab with sleeper tops and the LZB cable, `RKL` sleeper tops and
    the cable, `DB1` a ballast embankment with sleepers; all with a contact
    wire (`mt_cwire`). The README is dedicated "In memory of Norbert
    Rieger".
  - The shapes come with German high-speed routes on the-train.de: the
    dbtracks-extras README (<https://github.com/pgroenbaek/dbtracks-extras>)
    lists "The V4 shapes e.g. from the NIM, ICE-HB and Köln-Frankfurt
    routes" as a prerequisite (the usage round names NBS Köln-Rhein/Main,
    SFS Nürnberg-Ingolstadt-München and SFS Hannover-Berlin V3). The usage
    round reads this as: not in the seven DBTracks zips. The zips
    themselves could not be opened (403), so that is unchecked.
  - Pictures: `/home/arch/tsection-families-web/pictures/v4hs/` (2, Open
    Rails renders of the profile `TrProfile_V4hsR2k.stf`: double-track
    slab with sleeper blocks, rails on clamps, contact wire). No picture of
    the original shapes or the tunnels was found.
- Shapes: 160 / 160. None local. PRUDNIK: 0.
- Kind and gauge: track; 1.435 m (1.5); two tracks only, 4.985 m.
- Contents: parallel 80 (`V4hs2t_R2k_*`, `V4hs2t_NP1_*`: straights and
  curves 1000-15000 m), tunnel 80 (`TunS`, `TunR`, `_g` variants; `TunS`
  and `TunR` each have 26 curves and 9 straights, `_g` 4 curves and the
  10 m straight).
- Look: not local. `R2k` is probably Rheda 2000 slab track (still a
  guess); `NP1` has no sleepers in the profiles; `TunS`, `TunR` and `_g`
  are not explained (unconfirmed).
- Profile needs: slab families with side roles (`NP1` a plain slab, `R2k`
  a slab with sleeper blocks, as the Open Rails profiles draw them; rails on
  direct fastenings, no ballast); the contact wire (the Open Rails profiles
  sweep one); two tunnel tube families. No junctions in the set.

#### R.E.T. Metro (Roland Bal)

- Source: Roland Bal, RET Metro (developer list with
  http://www.retmetro.nl/...; Build 38 readme: "RET Metro (Commercial
  Add-Ons)"). RET runs the Rotterdam Metro, third rail on most lines
  (<https://en.wikipedia.org/wiki/Rotterdam_Metro>). High confidence for the
  set; the station abbreviations below match the route's own captions.
  - Route versions (download page,
    <https://web.archive.org/web/20070320215326/http://www.retmetro.nl:80/nl_downloads_route.htm>):
    V2 (28 Nov 2003, 63 MB) runs from Centraal Station to Zalmplaat, "maakt
    geen gebruik van Xtracks" and installs its own tsection; V3 Marconiplein
    to Capelsebrug; V4 Zalmplaat to De Akkers. V5 (beta, 1 Feb 2009, "the
    last route download for TrainSimulator") adds Capelsebrug to Binnenhof
    (Ommoord), driven "onder de draad" (under the wire) beyond Capelsebrug
    (news items 27 and 37). A news item on V4 says it "wordt ... niet
    commercieel gemaakt", against "Commercial Add-Ons" in the Build 38
    readme
    (<https://web.archive.org/web/20060113231824/http://www.retmetro.nl:80/nl_msts_nieuwsitem24.htm>).
  - The station and depot tokens match the V2 stations and depots named in
    the route's screenshot captions
    (<https://web.archive.org/web/20070223072749/http://www.retmetro.nl:80/nl_screenshots_route.htm>):
    Slg Slinge, Zpl Zuidplein, mhv Maashaven, rhv Rijnhaven, whp
    Wilhelminaplein, Lhv Leuvehaven, Brs Beurs, shs Stadhuis, CS Centraal
    Station, Zpt Zalmplaat, Hvt Hoogvliet, Twr Tussenwater, Ptg Poortugaal,
    Rho Rhoon, Hld remise Hilledijk, Whv remise Waalhaven. So the shapes are
    those of route version 2: the North-South line (Erasmuslijn) with the
    Hoogvliet branch (from the names; no page lists the shapes).
  - Packages: only V5.0 (130 MB) and its SP1 survive, in the Wayback Machine
    (<https://web.archive.org/web/20110528032057/http://www.busfoto.nl/40jaarmetro/RMversie5-0.exe>);
    V2 to V4 were not archived, and retmetro.nl is gone. The archived
    readmes of V2-V4 are PDFs (not read).
- Shapes: 147 / 147. None local. PRUDNIK: 0.
- Kind and gauge: track, viaducts, stations; 1.435 m (1.5); 4.985 m.
- Contents: single 59, parallel 46, junction 10, tunnel 30, special 2.
  Track `Ret*` with tokens `viad` (viaduct), `viadkol`, `viadbnl`, `tun`,
  `gri`, `gs`, `hld`, `lg`; switches and crossovers `Retkrswsl*`
  (kruiswissel); `Retcs*` (tunnel by flag); stations `RetStat*`
  (`Twr`, `Rho`, `Ptg`, `Zpt`, `Hvt`, `Brs`, `CS`, `Slg`, `Zpl`, `Lhv`,
  `mhv`, `rhv`, `whp`, `shs`: stations of the Rotterdam North-South line
  and its Hoogvliet branch, see Source);
  depots and yards `RetWhv*` (Waalhaven), `RetHld*` (Hilledijk),
  `Rethldremper`, `Retuit*`. Still unexplained: `gs`, `gt`, `gri`, `lg`,
  `kol`, `bnl`, `Dht`, `remper`, `BDperron`.
- Look: not local. The route's screenshots
  (`/home/arch/tsection-families-web/pictures/ret-metro/`, 11) show a third
  rail with an ochre cover on black brackets outside the running rails,
  broken at the points; a smooth brown bed without visible sleepers; pale
  grey rails; depot tracks on grey ballast; box tunnels with points inside;
  elevated stations on viaducts. Whether the conductor rail belongs to the
  track shapes or to separate objects is not stated.
- Profile needs: a metro family (with a conductor rail if the shapes carry
  it);
  viaduct families (deck and parapets as polylines); tunnel tubes;
  `RetStat*`, depot and yard buildings `DISABLED`.

#### Chris Jennings (NRa, NRA)

- Source: "Track Shapes by Chris Jennings" (`_INFO`; developer list:
  cjennings1@iinet.net.au, Australia). What `NR` means is not stated
  (unconfirmed).
  - The UKTrainSim library (archived 2006,
    <https://web.archive.org/web/20060523165229/http://www.uktrainsim.com:80/filelib-search.php?form_author=Chris+Jennings&CUTHERE=a20039306402bfbd619b0fd5a0afb7f9>)
    and trainsim.com show a Chris Jennings as the author of New South Wales
    rolling stock, 2002-2005: the XPT (UKTS 1465 and 3943; fid 53885) and
    Sydney CityRail V, M and S sets (fids 59140, 60289, 59138). Same name
    and an `.au` address, so probably the same person (unconfirmed). A
    Sydney underground prototype (City Circle or Eastern Suburbs Railway)
    would fit the tunnels, but no page says so (the trainsim.com round's
    guess). A Steam4me tunnel tutorial "by Chris Jennings" (2002) does not
    mention `NRa` (<https://nekocoaster.github.io/steam4me/tutorials/tunnel.html>).
    No package, route or picture was found.
- Shapes: 145 / 145. None local. PRUDNIK: 0.
- Kind and gauge: track, mostly tunnels; 1.435 m (1.5); two `NRA` at 1.0.
- Contents: tunnel 84 (`RndTun`, `Rndtun`, `DividedRndTun` two tracks with
  a dividing wall, `HalfRndTun`, `NoWallsRndTun`, `TunBase`, tunnel points
  and buffer), single 20, parallel 38 (`NRA` 120 m curves, `NRa6t250r5d`),
  slips `NRa1DoubleSlip`/`SingleSlip` with ends.
- Look: not local.
- Profile needs: round-tunnel tube families (single, divided two-track,
  half, base only, no walls); `COMPLEX` for the slips; a metre-gauge member
  for the two 1.0 shapes.

#### Jeffrey Morris (tunnel, switch, substation)

- Source: "Track Shapes by Jeffrey Morris" (`_INFO`; allocation 19175-
  19499; also 18 `A1t62r*tun` shapes in `A`). Prototype not stated. All
  141 shapes are already in Build 38 (November 2007). No package, route or
  picture was found. The developer-list address (`si.rr.com`) is a Road
  Runner address of Staten Island, New York (an inference of the picture
  search, unconfirmed).
- Shapes: 141 / 141. None local. PRUDNIK: 0.
- Kind and gauge: tunnels and structures; 1.435 m (1.5).
- Contents: `tunnelNN-*` (117; `NN` = number of tracks 01-08, at 4.985 m;
  `co` pieces, `bumper` ends, `end` pieces), `switchNN` (13, single or two
  paths, one or two straight sections; what is built on them is unknown),
  `substationNN` (11, 2-3 paths, e.g. at -9.97, -4.985, +4.985 m).
- Notes: none of the 117 tunnels has the `TunnelShape` flag; they are
  tunnels by name only.
- Profile needs: tunnel tube families chosen by `tunnel*` filters (not by
  `Kind`); `switch*` and `substation*` `DISABLED` until their content is
  known.

#### UK narrow gauge (Tim Bridge, ukn)

- Source: Tim Bridge, UK Narrow Gauge ("Tim Bridge - UKN* narrow gauge
  track sections"; Andrew Gadd "point animation fix for UKN* and new UKN*
  shapes"), merged into XTracks (credits and versions pages); 32 shapes by
  Okrasa Ghia. British narrow gauge. High confidence. Build 54 marks the
  UKN sections from 39327 on "added by Andrew Gadd and Okrasa Ghia".
  - Use: little, but always installed with XTracks. The Corris No. 3 scale
    patch (2002) "requires UK_NG track"
    (<https://archive.org/download/ukts-archive-files-1001-2000/UKTS_1866_ngcorris3.zip/corris3/ngReadme.txt>).
    Reg Furniss reworked `ukn` in 2012 (fid 28317) for a Talyllyn 1940s
    route that was not found released; Talyllyn 1.5 was built at double
    scale on standard track.
- Shapes: 127 / 127 (`ukn` 125, `uknA` 2). All local. PRUDNIK: 0.
- Kind and gauge: track; 2 ft 4 in, 0.7175 m (0.75); spacing 2.4925 m.
- Contents: single 48, junction 16 (10° points, 3°/6° modular, stubs, Y,
  yard), diamond 4, frog piece 20, tunnel 34 (`Tun`, `RndTun`, "UKNG Large
  Tunnels"), special 5 (buffers `ukn1tBuffer`/`ukBuffer`/`USBuffer`, level
  and pedestrian crossings); `uknA1tXover6d*` crossings with standard
  gauge.
- Look (looked at `ukn1t125r5d`, `ukn1tPnt10dRgtMnl`, `ukn1t500r5dTun`,
  `ukn1t125r5dRndTun`, `uknA1tXover6dLft`): 3 m bed at y 0.26
  (`UKNCleanTrack1`: wooden sleepers with chairs painted, pink-grey
  ballast), rails to 0.32; tunnels with earth walls to 5.2 m, round to
  3.1 m.
- Profile needs: a 0.7175 m family; tunnel tubes; `COMPLEX`; two-gauge
  crossings for `uknA`.

#### Metre gauge with third rail (Etienne Bruère, tj)

- Source: "Metric Gauge Track Shapes with 3rd rail by Etienne Bruere"
  (`_INFO`; developer list with http://www.funtrain.net/index.php?cible=ligne&id=40).
  French. That link was the download page of "La ligne de Cerdagne Version
  2" by "Richard GARCIA, Etienne Bruere, Raymond (RM77)" (archived
  2006-2007). So `tj` is the Train Jaune: metre gauge with an 850 V third
  rail (route readme). The presentation page
  (<http://web.archive.org/web/20080420233032/http://www.funtrain.net/index.php?cible=presentation&id=40>)
  says "Nécessite : Xtracks 3.16" and that this version covers "Saillagouse
  - La tour de Carol"; V2 dates from 2005. funtrain.net has renumbered its
  lines: the route is now line 27 (<https://api.funtrain.net/lines/27/>),
  `Cerdagne.exe` (69 MB, file date 2021-09-23), which appears to carry its
  own track ("une version plus ancienne des voies nécessaires à la
  ligne"; not downloaded). All 100 shapes are in Build 38. High
  confidence.
- Shapes: 100 / 100. None local. PRUDNIK: 0.
- Kind and gauge: track; 1.0 m (1.0).
- Contents: straights, 70/100/200 m curves, 12° points, crossovers, tunnels
  and tunnel entries, an inspection pit `tj1tpit`; third-rail variants
  `3p`/`3u` (and `3pi`, `3po`, `3ui`, `3uo` on curves: inner/outer,
  meaning of `p`/`u` unconfirmed), `3ps`/`3us` pieces; `tj1txoverA1t10d`
  crossing with standard gauge.
- Look: not local. The route's screenshots
  (`/home/arch/tsection-families-web/pictures/tj-bruere/`, 3) show a side
  conductor rail along the sleeper ends of metre-gauge track, broken at a
  platform crossing.
- Profile needs: a metre-gauge family with a conductor rail part by side;
  tunnel tubes and portal pieces (`TunEntry`, static); the pit (not
  covered: a cut in the bed); a two-gauge crossing.

#### CYKWORKS subway and El (Christopher Cyko)

- Source: Christopher Cyko, CYKWORKS: "Chris Cyko - CYKWORKS, C* & L*
  elevated track sections", merged into XTracks 3.13 (XTracks pages);
  local readme "CYKWORKS Track Pack 2.0 ... specifically designed for use
  with Chicago Elevated Railways packages" (`tsdata/global/textures/readme.txt`).
  Chicago "L". High confidence.
  - Packages on trainsim.com: Track Pack 1 (fid 59089, `cyktrck1.zip`,
    2003-07-28, "third rail and tight radius Switches") and Track Pack 2.0
    (fid 59401, 2003-12-24); the route Chicago Elevated Railways v0.4 (fid
    60725, 2003), v0.6d (fid 59386) and v2.0 (fid 57474). The Build 38
    readme: the original L2 route (2002) is "not compatible", Cyko's work
    after July 2003 is. Dormant since (a 2012 post:
    <https://forum.trains.com/t/226739.json>). Picture:
    `/home/arch/tsection-families-web/pictures/cykworks/` (the route's
    thumbnail).
- Shapes: 84 / 0 (`C` 52, `L` 16, `Cp` 9, `Croswab` 3, `LHalOR` 2,
  `Cperoswab` 1, `Cpbasbr` 1). All local (OR_PSK_NEW, tsdata). PRUDNIK: 0.
- Kind and gauge: elevated track, subway, stations; 1.435 m (1.5; some
  1.8). Spacing 3.66 m (`C2t`), 4.985 m (`L2t`).
- Contents: `C*el`, `C*eg`, `C*en`, `C*ea`, `C*eso`, `C*ssm` elevated
  structures; `C*sub*`, `C*cub*`, `C*sq*` subway pieces (27 tunnel-flagged);
  `L*` track, `L*tunstrt` (tube, no flag), `L*orc`, `L*cel`, `L*CCsta`;
  `Cp*` points (20°, Y); `Croswab*`, `Cperoswab`, `Cpbasbr`, `LHalOR*`
  stations and buildings with track.
- Look (looked at `C1t50mel`, `C2t50meg`, `C1t10mstrtsub`, `C1tsqp`,
  `L1t10mstrt`, `L2t10morc`, `Cp20drgt`, `Croswab1`): 3D wooden ties
  (`wood.ace`, y -0.10..0.20) without ballast on a steel structure
  (`e1y.ace`, to y -1.0); rails `ACleanTrack2` with a third rail at
  x +1.05..1.15 m up to y 0.5; subway walls (`ssub`, `rwall`) to 4.7 m;
  stations with platforms, roofs, walls (`Croswab1` reaches 12 m below
  and 5.6 m above the rail base).
- Profile needs: a family with 3D ties (`Repeat`), no ballast, a third rail
  part; an El family adding the steel structure (polylines or `Sweep`);
  subway box tunnels; `COMPLEX` for `Cp*`; stations `DISABLED`.

#### XTracks virtual tunnel (OBY, VT)

- Source: "XTracks virtual tunnel by OBY" (`_INFO`); indices in Alessandro
  Baldi's range (19850-20849), right after "_INFO(Track Shapes from
  ilDeposito)". OBY is a co-author, with Renzo Grassi and Alessandro Baldi,
  of ilDeposito's "VT-Tracks e IT-Tracks" (see the Italian set); who OBY is
  beyond the nickname is not known. `VT` is not in Build 38 (November
  2007); ilDeposito says routes from 2009 on need the package. Italian
  routes of 2009-2018 use it (see "Use today").
- Shapes: 77 / 0. None local. PRUDNIK: 0.
- Kind and gauge: track with `TunnelShape`; 1.435 m (1.5).
- Contents: straights, curves 120 m and up, 6° points, crossings, buffer,
  end points, all tunnel-flagged.
- Look: not local. "Virtual tunnel" by its name: track that MSTS treats as
  a tunnel, inside tunnels built from other objects (unconfirmed; the
  package says only "Virtual Tunnel"). No picture was found.
- Profile needs: track only (no tube) by a `VT*` filter before tunnel
  filters.

#### DualTracks (William Adams, D)

- Source: William 'Bill' Adams, DualTracks, "a set of tracks for three rail
  dual track routes combining normal and narrow gauge tracks with a third
  rail", included in XTracks since 3.9 (<https://www.nklj.se/xtracks/documents/dualtracks.htm>);
  18 diverging switches by Jared Singletary. The XTracks page points to
  Bill's site "Simulated EBT" (East Broad Top, US, by our reading); his East
  Broad Top project was not released. High confidence.
  - Packages on trainsim.com: DualTracks v1.1 (fid 61072, `dtrack11.zip`,
    2004-01-31) and the reference route the XTracks page names, "Reference
    Route v1.0" (fid 57842, `dtref10.zip`, 2003-02-22).
  - Use: La Crosse Division (Milwaukee Road, Todd Jones, 2003, fid 58902):
    "Requires XTracks (XTRACKS.ZIP) and Dualtracks (DTRACK10.ZIP)". The MBTA
    route (fid 63284, 2004) and SA Adelaide, Hills & South East (fid 90534,
    2025) ship `D1tPnt*` pieces; Trento-Malè (fid 88232) ships
    `gf_d1tpnt*` reskins.
  - Pictures: `/home/arch/tsection-families-web/pictures/dualtracks/` (3).
- Shapes: 71 / 53. Local 53 of 71. PRUDNIK: 0.
- Kind and gauge: track; 1.435 m and 0.861 m with a common rail (0.9 1.5).
- Contents: straights and curves `LC`/`RC` ("left common" / "right
  common"; on curves inside/outside common), 10° points with LC/RC,
  `PntEnd`, derails, "Center" pieces (narrow gauge centred in standard),
  0° points; 9 Build 54 entries have missing sections.
- Look (looked at `D1t12mStrt`): 5 m Kuju-style bed (`DCleanTrack1`) at
  y 0.20, rails to 0.33.
- Notes: every dual straight and curve is in group 5 (two paths 0.287 m
  apart, closer than a tie), so a plain dual track is `Kind ( Complex )`.
- Profile needs: a three-rail profile; the generator must draw the common
  rail once where two paths' rails coincide (not in the design); `COMPLEX`
  ties shared by both gauges (correct by the current rule).

#### 600 mm Feldbahn (Lutz Schwerthelm, lsfb)

- Source: "600mm Feldbahn - narrow gauge - by Lutz Schwerthelm" (PSK
  header and `_INFO`). German field railway. PSK only. No released route
  uses it; the Polish Podlasie project (trainsim.com forum thread 155079,
  2022-2024) plans "Feldbahn tracks for Hajnówka 600 mm gauge forest
  railroad".
- Shapes: 0 / 53. Local 51 of 53. PRUDNIK: 0.
- Kind and gauge: track; 0.6 m (SectionSize 0.5, 0.75 and 0.9 mixed).
- Contents: single 43 (curves from 10 m radius), junction 10 (3° points,
  25 m points, stubs, 30° tram points).
- Look (looked at `lsfb1t15mStrt`, `lsfb1tPnt3dLftMnl`): a 2 m bed at
  ground level (y 0.0, `lsfbcleantrack1`), light rails 0.20-0.30 at
  ±0.34 m.
- Profile needs: a 600 mm family with the bed at ground level, no ballast
  shoulder; `COMPLEX` for points.

#### Schwebebahn (Sjoerd Ydema, WS)

- Source: "Schwebebahn Trackshapes SYdema" (`_INFO`; allocation "Sjoerd
  Ydema Schwebebahn"). A suspended monorail. The prototype is the Wuppertal
  Schwebebahn: HCC!trainsim's article of 16 Aug 2007 quotes the author
  (Sjoerd Ydema, Dutch), who made "alle rails, alle haltes, de twee depots
  en de trein" himself and got the tsection entries right "met wat hulp van
  het forum van Retmetro.nl". All 36 shapes are in Build 38.
  - The route is downloadable: HCC!trainsim, "Wuppertal Schwebebahn v1"
    (`Schwebebahn.rar`, needs XTracks 3.18, re-posted 2017,
    <https://trainsim.hcc.nl/downloads/microsoft-trainsimulator/routes-voor-microsoft-train-simulator/154-wuppertal-schwebebahn-v1/file>),
    with a tsection update (XTracks 3.18's file plus the Schwebebahn
    entries); and trainsim.com fid 66942 (`schweb1.zip`, 2007: "the full
    route (20 stations and two depots) and the train").
  - Pictures: `/home/arch/tsection-families-web/pictures/schwebebahn/` (8,
    from the HCC article): green steel girder track, two tracks side by
    side, the train hanging below. Whether the girders are part of the `WS`
    shapes is not stated.
- Shapes: 36 / 12. None local. PRUDNIK: 0.
- Kind and gauge: other; paths at 3.51 m spacing; "gauge" 0.5.
- Contents: straights `S`, curves `C` (7.5-100 m), points, depot points.
- Profile needs: not a track profile. `DISABLED` (or a STATIC structure
  family later).

#### LUL tube tunnels (Richard Garniss, lultube_)

- Source: "Shapes From Richard Garniss LUL Project" (`_INFO`; allocation
  19100-19174). London Underground deep tube by name. All 30 entries are
  in Build 38. No package, route or picture was found. Sjoerd Ydema's
  "London Underground: Northern Line" v1.2 (fid 19204, 2009) ships its own
  route tsection.dat, and its zip lists no `lultube` shape; the UKTS
  District Line beta has no tube pieces either. A 2008 forum mention of a
  planned "LUL track pack" for UKFS with four-rail track (District Dave's
  forum, search summary only) is not tied to Garniss.
- Shapes: 30 / 30. None local. PRUDNIK: 0. In PSK 17 of the 30 entries
  point to missing sections (group 0).
- Kind and gauge: track in tube tunnels; 1.435 m (1.435).
- Contents: `stun` straight tube, `ctun`/`ctunl`/`ctunr` curved tube,
  `stntun`/`stnctun` station tunnels, `stunend*` ends, `tl`/`tr` 11d42
  turnouts in tube.
- Profile needs: a circular tube family; LUL four-rail track (third and
  centre fourth rail); `COMPLEX` inside a tube; station tunnels and ends
  static.

#### Steven Masters F (ferry and carfloat track)

- Source: "Track Shapes From Steven Masters Ferry Tracks" and "... Track
  Additions to Xtracks" (`_INFO`; allocation "Steven Masters Ferry").
  US carfloat practice by the 11.5 ft (3.5052 m) spacing. Ships in XTracks
  3.19; no route using it was found.
- Shapes: 27 / 14. Local 14 of 27. PRUDNIK: 0.
- Kind and gauge: track on a vessel deck; 1.435 m (1.5).
- Contents: `F<n>tFerry*` 3-5 track ferry decks, `F3t*mStrt`, three-way
  points `F1tPnt3w*` with `_Fixed` pieces, Y points, S curves `F1tS*`.
- Look (looked at `F1tSLft`, `F1tPnt3wLftRgtMnl1`, `F1tYPnt`,
  `F3tFerry03`, `F3t05mStrt`): rails only (`ACleanTrack2`), no bed, except
  the three-way points.
- Profile needs: a rails-only profile (no bed, no ties) or `DISABLED`.

#### Lartigue monorail (Andrew Gadd, LM, Lm)

- Source: "THE LARTIGUE MONORAIL SYSTEM", "Track Shapes By Andrew Gadd"
  (`_INFO`; allocation "Andrew Gadd Lartigue Monorail"). The Listowel and
  Ballybunion Railway is the best-known Lartigue line (prototype
  unconfirmed).
  - A UKTrainSim upload "Lartigue" by andrewgadd existed: its 150 px
    thumbnail survives in the Wayback Machine
    (`/home/arch/tsection-families-web/pictures/lartigue/`): a single rail
    on a row of A-frame trestles at about waist height, a straddling
    locomotive, a lattice footbridge. The file entry itself was not found.
    A trainsim.com forum thread of 2005-04-27 (28799, "A Lartigue Monorail
    (from Bodgit & Scarper Engineering)", signed "Andrew.") shows Lartigue
    trackwork "including animated turntable turnouts" in a "very British"
    narrow-gauge route, probably the author's work in progress.
  - Andrew Gadd was a UK narrow-gauge modeller (a 15 inch gauge route,
    narrow-gauge textures, Decauville and Dick Kerr stock, 2003-2004) and
    added `UKN` shapes to XTracks
    (<https://www.nklj.se/xtracks/documents/credits.htm>). All 24 shapes are
    in Build 38.
- Shapes: 24 / 0. None local. PRUDNIK: 0.
- Contents: straights, curves 30-150 m, end rails, crossing gap, turntable
  and traverser points, rails.
- Profile needs: not a track profile. `DISABLED`.

### 3. Small sets

| Prefix / shape | Set | Shapes B54 / PSK | Local | PRUDNIK | What it is | Profile |
| --- | --- | ---: | --- | ---: | --- | --- |
| `Osmannoro_OML` | Raffaele Fiore, "Additional Track and Road Sections ... xHighways" | 1 / 0 | no | 0 | two paths; Osmannoro is near Florence (name only) | unknown; `DISABLED` |
| `SR_River` | ScaleRail ("ScaleRiver Sections") | 1 / 0 | yes | 0 | 100 m water plane with two crossing paths | `DISABLED` |
| `SR_InvisiTrack_` | ScaleRail | 2 / 0 | yes | 0 | invisible track (`SR_InvisiTrack.ace`) | empty profile or `DISABLED` |
| `SR_Turntable_c_`, `_w_` | ScaleRail | 6 / 0 | yes | 0 | 37 m turntable and ends | `DISABLED` |
| `EB_1tWireEnd_LG` ... `4t` | Europeanbahn | 4 / 0 | no | 0 | overhead wire ends with lattice gantry | `DISABLED` |
| `eb_A2tPnt10d*Tun` | Europeanbahn | 2 / 0 | no | 0 | two-track tunnel points | tunnel, `COMPLEX` |
| `I1t20mStrt` | XTracks | 1 / 1 | yes | 0 | invisible track (transparent bed) | empty profile or `DISABLED` |
| `NA1tXover6dLft`/`Rgt` | XTracks twin gauge | 2 / 2 | yes | 0 | standard x 3 ft gauge crossing | two-gauge `COMPLEX` |
| `uknA1tXover6dLft`/`Rgt` | XTracks twin gauge | 2 / 2 | yes | 0 | standard x 2 ft 4 in crossing | two-gauge `COMPLEX` |
| `tj1txoverA1t10d` | Etienne Bruère | 1 / 1 | no | 0 | metre x standard crossing | two-gauge `COMPLEX` |
| `TramZ_A1t12_5mstrt` | Ralf Riemer | 1 / 1 | yes | 0 | tram rails only | rails-only profile |
| `Croswab`, `Cperoswab`, `Cpbasbr`, `LHalOR` | CYKWORKS | 7 / 0 | yes | 0 | stations, platforms, buildings with track | `DISABLED` |
| `substation*` | Jeffrey Morris | 11 / 11 | no | 0 | substations on 2-3 tracks (by name) | `DISABLED` |
| `RetStat*`, `RetWhv*`, `RetHld*`, `Rethldremper`, `Retuit*` | R.E.T. Metro | 53 / 53 | no | 0 | stations, depot, yard pieces | `DISABLED` |
| `hunBuffer5`, `hunBuffer6`, `*Concrete` | PSK "NEW Concrete small pieces" | 0 / 4 | yes | 0 | Hungarian buffer stops on short track, wood and concrete | track profile; buffer mesh later |
| `OdbW_1t1mStrt`/`3mStrt`, `OdbZ_1t1mStrt`/`3mStrt` | PSK "kozly oporowe" (buffer stops) | 0 / 4 | no | 0 | 1 m and 3 m buffer-stop pieces (W/Z types) | track profile; buffer mesh later |
| `Wk1Pe_1t1_2m` | PSK "wykolejnice" (derails) | 0 / 1 | no | 0 | 1.2 m derail (two paths, group 3) | track profile; derail mesh later |
| `Wk_brama_wj*`, `Wk_bramaMD-1_nr1` | PSK "bramy itp." (gates) | 0 / 4 | yes | 1 | track through a gate (`plot_bet_ozd`, `mosaic1`), two paths (group 3) | `DISABLED` |
| `_depo-3t_80m_1` | PSK "hale zaplecza warsztatowego" | 0 / 1 | yes | 0 | 80 m three-track depot hall with pits (9.9 m tall) | `DISABLED` |
| `a1t15_2mstrt_D` | PSK "small wood straight" | 0 / 1 | yes | 0 | A-namespace wooden straight with a lower-case name | the A wood profile (`a*_D` filter) |
| `DB2_a1tPnt4_7d500r` | PSK ("Krakow_Kocmyrzow ...") | 0 / 1 | no | 0 | DBTracks-named point; its sections are missing | none |

### 4. Open points

1. Gauge is not in the data. SectionSize writes 1.5 for standard gauge and
   for ScaleRail's 30" and 36" narrow gauge, 0.9 for 0.861 m, 0.75 for
   0.7175 m, mixed values for the 600 mm Feldbahn, 0.0 for Lartigue. The
   design samples rails "at the route's gauge" (task 17, sections 2-3); a
   profile (or the filter) must carry the gauge: N, ukn, D, VM, VD, MT/Z,
   tj, lsfb, SRn30/SRn36 need it.
2. Two gauges in one shape: DualTracks (three rails, a common rail), and
   the crossings `NA*`, `uknA*`, `tj1txoverA*`, `VM1tA1tX90d`. The rail
   generator must merge coincident rails of different paths and take a
   gauge per path. Plain dual track falls in group 5, so `Kind ( Complex )`
   catches ordinary dual-gauge straights.
3. `TunnelShape` is not "has a tube". Flagged but without walls: XTracks
   `S*` (120), `VT*` (77), PSK `N*Tun` "tun_ghoost" (19), `eb_*_vtun`,
   `TramB_A2t12_5mstrtTun`, and 70 PRUDNIK `DB30*` objects on S indices.
   Tubes without the flag: Morris `tunnel*` (117), Cyko `L*tunstrt`, some
   `SR_2tTun_*_Crv_*`. A `Kind ( Tunnel )` filter giving a tube family
   would be wrong for the first list and miss the second; filters must go
   by name, or `Kind` needs a wall test.
4. Tunnel tube shapes differ by set: Kuju square, round (`RndTun`),
   flat-topped 6.5 m, wired (`TunFW65`), divided two-track, half-round,
   base-only, no-walls, ScaleRail 8.2 m, London tube, subway box, HSR. Most
   are polylines; portals, ends and entries (`TunEntry`, `stunend`,
   `bumper`, `end`) are not sweepable and stay static.
5. Conductor rails (third and fourth rail) are not in the design: UK
   FineScale `3r`/`4r`, Europeanbahn `i`/`o`/`3r`/`4r`, tj `3p`/`3u`,
   CYKWORKS `L`, LUL, probably RET. In plain track a polyline or `Sweep`
   at an offset works; the side comes from the name; at junctions the
   conductor rail has gaps and ramps (task 17's decision 24: a `COMPLEX`
   part cut at other tracks; ramps later).
6. Rack rail (`Z*`): a centre rack part; in rack points the design has no
   rule for which path keeps the rack (decision 24: cut at other tracks for
   now).
7. Overhead contact wire inside track shapes: about 15,100 PRUDNIK objects
   use DBTracks `f` skins, and Europeanbahn has wired tunnels. A track
   profile can sweep a wire on plain track; junctions need a wire plan
   (not in the design). Masts are separate objects.
8. Side roles and spacing: sets use 2.4925 m (ukn), 2.991 m (N), 3.0 m
   (tram), 3.413 m (UKFS, BR; UKFS also 4.632 and 5.242 m), 3.5052 m
   (ferry), 3.51 m (WS), 3.555 m (Italian), 3.66 m (Cyko), 3.9624 m
   (ScaleRail yard), 4.27 m and 7.62 m (MTracks), 4.985 m (Kuju), 5.0 m
   (DR). A `LEFT`/`MIDDLE`/`RIGHT` family's shared bed fits
   one spacing; families per set, or a bed that stretches to the actual
   spacing like the `COMPLEX` bed, are needed.
9. Slab track (V4hs), street track (Tram_A paving, TramBB kerb), open
   bridge decks with guard rails (ScaleRail `Brg_n`), 3D ties without
   ballast (CYKWORKS) all fit the TrProfile format as polylines and
   `Repeat`; grooved rail and check rails need their own rail meshes
   (`GuardRail` is phase 2 in the design; UK FineScale check-railed curves
   need it on plain track too).
10. Track with structures: embankments (`DB2he_*_emb_z`, 63 PRUDNIK
    objects), viaducts (RET), elevated steel (CYKWORKS), stations (RET,
    CYKWORKS), depot hall and gates (PSK), substations (Morris). They need
    `DISABLED` filters or structure polylines; the design lists only
    turntables, transfer tables and the roundhouse as static.
11. Not track: Schwebebahn `WS`, Lartigue `LM`/`Lm`, `SR_River`, ferry
    decks `F*Ferry*`, invisible track (`I1t20mStrt`, `SR_InvisiTrack_`,
    dummies). They need `DISABLED` or an empty profile; the filter file has
    no "draw nothing" value apart from the static shape.
12. Unidentified or unconfirmed. The second round (2026-11-26/27) answered
    or narrowed:
    - "UZK 3.18" in the PSK header: ÚZK, a Czech re-texture of XTracks
      geometry (A namespace entry);
    - `DR`: probably "dutchrail", the author's library name (`DR_` entry);
    - who OBY is: a co-author of ilDeposito's VT/IT package, nothing beyond
      the nickname (`VT` entry);
    - Chris Jennings: probably the author of Sydney rolling stock; his
      prototype is still unknown (Jennings entry);
    - BR Rail: the package was found (BR entry), but its suffixes are not
      explained; only that `a` and `gw` carry more geometry;
    - V4hs `NP1`: a slab without sleepers in the Open Rails profiles
      (V4hs entry);
    - not on this list before, but confirmed: `tj` = Train Jaune and the
      Wuppertal Schwebebahn (their entries).

    Still open: the `DR_` letters `t n r y s h`; BR Rail suffixes
    `a b g w d`; ScaleRail `y` and switch tokens `m`/`a`/`i`; Via Metrica
    `C`/`Ox`/`Sox`; V4hs `TunS`, `TunR`, `_g` (and `R2k` = Rheda 2000 is
    still a guess); Morris `switch*`; PSK skins `PL*` and `DB2he`. The
    authors' sites (europeanbahn.co.uk, retmetro.nl, funtrain.net's old
    pages, hcl.li, ukfs.trainsimfiles.com, home.comcast.net/~markhyams) are
    gone; the second round read most of them in the Wayback Machine or a
    mirror.
13. Locally missing sets: UK FineScale, BR Rail, Bjorn van Steenbergen,
    Via Metrica, V4hs, RET, Jennings, Morris, Italian, tj, VT, WS, LUL,
    Lartigue have no shape files here. Their looks come from names,
    documentation and, since the second round, pictures in
    `/home/arch/tsection-families-web/pictures/<set>/` (third-party, kept
    outside the repository; contact sheet
    `/home/arch/tsection-families-web/reports/pictures-sheet.jpg`):
    - `ukfs` (11, the author's pages), `br-rail` (2 untextured renders),
      `ret-metro` (11 route screenshots), `tj-bruere` (3 route
      screenshots), `schwebebahn` (8), `v4hs` (2 renders of the Open Rails
      profiles, not of the shapes), `italian-alexb` (2, of the sibling
      `Ca` set), `via-metrica` (1, low confidence), `lartigue` (1
      thumbnail);
    - none for `DR_`, Jennings, Morris, LUL and `VT`.

    Packages that exist (none downloaded): UK FineScale (fid 11889),
    BR_Project_Complete (fid 22268), RET Metrolijn V5.0 (Wayback Machine
    only), `Cerdagne.exe` (funtrain.net line 27; presumably holds the `tj`
    shapes), the Schwebebahn route (HCC!trainsim, fid 66942), VT-Tracks e
    IT-Tracks (ilDeposito), the German HSR routes with V4hs (the-train.de),
    Via Metrica inside the Dolomiti route. None was found for `DR_`,
    Jennings, Morris, LUL and Lartigue.

## Part 2. Road families (RoadShape-only prefixes)

Scope: the 51 name prefixes whose shapes all carry `RoadShape ( )`:
3,713 shapes in Build 00054, 2,430 in PSK 2.15, 3,736 different names in
both. The road prefixes of ScaleRail (`SR_Rural_`, `SR_City_`, ...) and of
Bjorn van Steenbergen's set (`DR_Rd_Type`) are in this list. Who made those
two sets and what their track is belongs to the rail section; here only
their road pieces are described.

How the numbers were made (all read-only):

- Lane layout per shape: each path's start offset, heading and sections from
  both `tsection.dat` files, sampled with
  `extra/tsection-review/tsection.py`. Each shape got one class:
  - plain: one path, or paths parallel from end to end;
  - junction: a path on another axis (T, X, skew crossings), or a path that
    ends at another heading (Y, exits, merges);
  - lane change: paths parallel at both ends, but their spacing or their end
    points change (lane added or dropped, taper);
  - definition error: a curve whose lane paths use radii that do not fit
    their offsets;
  - and, by name only, for shapes with plain paths: intersection-kit pieces,
    painted-marking pieces, transitions to another road type.
- Local shapes: meshes read with `assembly.shape_triangles`, and top-down
  renders with PRUDNIK's textures. The shapes looked at are named in each
  family.
- PRUDNIK: 13,056 road TrackObjs in `ROUTES/PRUDNIK/WORLD`, counted by
  `SectionIdx` (geometry, as in the CSVs) and by `FileName`.
- Sources: the NewRoads installation guide
  (https://nekocoaster.github.io/steam4me/routes/newroads.html); the NewRoads
  Route Builders Kit 3.2, North America
  (https://nekocoaster.github.io/steam4me1/routes/files/rdsrbkna.zip, 2004).
  The kit has `Notes.doc` by Steven Masters, which describes every NewRoads
  set with widths and textures, the original readmes by Martyn T. Griffin and
  Bruce Bridges, and pictures of the intersection kits. Also the
  developer list at the top of Build 00054 and the `_INFO` lines. The
  first round's web searches for the smaller sets (J.A. Norton, Dale
  Rickert, R. Fiore, Graham Pitt, Norbert Rieger's Autobahn, the Kraków
  set) found nothing. The second round (Wayback Machine, trainsim.com; see
  "Sources and method") found Graham Pitt's kits, Rieger's Autobahn pages
  on dbtracks.com, Dale Rickert's route and the Kraków route's author;
  their entries give the sources. For J.A. Norton and R. Fiore everything
  below still comes from the tsection credits and the names, and is marked
  so.

Data: per-shape lane layouts (`roads-b54.json`, `roads-psk.json`), the classes
(`classes.json`) and PRUDNIK counts (`prudnik-roads.json`) are in the
assets-gen repository, `reports/tsection-families/road-data/`; top-down
renders of some shapes in `reports/tsection-families/road-renders-1.png`
and `road-renders-2.png`.

### 1. Summary

The gauge is the first value of `SectionSize`. It means nothing for roads:
1.5 everywhere, except KK_Alpl's own curve sections (0.5) and three SR_Dirt
sections (1.435). Local counts are shapes found in at least one game root.
"Junction-like" = junction + lane change (section 2.0, item 5).

| Family | Prefixes | Kind | Gauge | Shapes (B54 / PSK) | Found locally | PRUDNIK objects | What a profile needs |
| --- | --- | --- | --- | ---: | --- | ---: | --- |
| Kuju default roads + NewRoads additions | `Road`, `DirtRoad`, `JP`, `US` | road | 1.5 | 156 / 144 | 144 (77 Kuju originals in MSTS) | 9,586 | one flat strip, 6 or 12 m at y 0.1, the route's texture; 2-lane LEFT/RIGHT; 12 T/X junctions; 8 bad 4-lane curves |
| NewRoads local roads (Masters, Griffin, Rice, Curphey, Bridges) | `cr`, `pr`, `Urban`, `br`, `dr`, `gr`, `rd`, `prrw` | road | 1.5 | 589 / 589 | 587 | 2,975 | flat strips 3-14 m (dirt, gravel, cobble, country, urban one-way and two-way, city with kerbs in the texture, runway); intersection kits; 77 junction-like |
| NewRoads highways and freeways (Masters, Curphey, Kraus-Yao) | `hwy`, `fwy`, `Fwy` | road | 1.5 | 1,152 / 1,142 | 1,138 | 194 | carriageway + 2 m sloping shoulders; one-way carriageways with l/r curve pairs; medians 3-10 m; intersection kits, exits, merges; 105 junction-like |
| German Autobahn (Norbert Rieger) | `BAB` | road | 1.5 | 406 / 406 | 406 (+ 5 folder variant sets in OR_PSK_NEW) | 225 | 3D: hard shoulder, embankment, central reservation with steel barriers and hedge; one or both carriageways; 16 lane add/drop pieces |
| ScaleRail road pieces | `SR_Rural_`, `SR_Town_`, `SR_City_`, `SR_Urban_`, `SR_Dirt_`, `SR_Hiway_`, `SR_Inter_`, `SR_Access_`, `SR_Country_`, `SR_Parkway_` | road (of a track-and-road set) | 1.5 (3 sections 1.435) | 739 / 0 | 681 (TS_STARTER_ROUTE, textures missing) | 0 | 3.65-3.9 m lanes; kerbs 0.15 m and sidewalks, guard rails, raised median; 75 junction-like; marking variants |
| J.A. Norton roads | `SLW_UR_`, `SLW_NC_`, `SLW_SB_`, `SLW_FW_`, `SLW_`, `SLW_UX_`, `SLW_NX_`, `SLW`, `SLW_UR_SB_` | road | 1.5 | 245 / 0 | 0 | 0 | 3.65-3.9 m lanes (US); 6-lane freeway with a 10.7 m median and half pieces; 46 junction-like |
| Dale Rickert roads | `RR_Rur_a_`, `RR_Int_a_`, `RR_Int_c_`, `RR_Int_a_m_`, `RR_Int_c_m_`, `RR_City_`, `RR_Rural_` | road | 1.5 | 261 / 0 | 0 | 0 | single-path rural road; divided 4-lane with 4.7 or 15.9 m median; 7 junction-like |
| Bjorn van Steenbergen roads | `DR_Rd_Type` | road (of a track-and-road set) | 1.5 | 94 / 94 | 0 | 0 | road types 1-4, Dutch names; exits with parallel roads; 17 junction-like; paths not listed left to right |
| R. Fiore Italian roads | `xroad`, `xhwy`, `xhwyt` | road | 1.5 | 39 / 0 | 0 | 0 | 3.5 m lanes; dual carriageway; bridge and tunnel pieces |
| Kraków-Kocmyrzów avenue | `KK_Alpl` | road with tram track | 1.5 / 0.5 | 0 / 23 | 23 | 76 | 42.5 m avenue: two carriageways, ballasted tram track in the middle, tram stop, side street; keep static first |
| Magnetic platforms (Graham Pitt) | `Plat`, `WhitePlat`, `MarblePlat`, `AshphaltPlat` | other (platforms flagged as road) | 1.5 | 32 / 32 | 0 | 0 | platform cross-section as ROAD MAIN, one path |
| Total | 51 prefixes | | | 3,713 / 2,430 | 2,979 | 13,056 | |

### 2. Families

#### 2.0 What all road families share

1. **Geometry.** A road shape has one path per lane, placed at the lane's
   centre. The paths of one shape all start in the same direction (8 shapes
   have one reversed path). The direction of traffic is not stored anywhere
   in `tsection.dat`. One-way or two-way comes only from the name (`1w`/`1W`
   one carriageway, `2w` two-way, `LT` both carriageways) and from the
   texture's markings.

2. **Lane widths (path spacing) differ by family.** A profile with fixed
   lane width fits only the families with that spacing.

   | Spacing | Families |
   | --- | --- |
   | 3.0 m | Kuju (`Road`, `DirtRoad`, `JP`, `US`), all NewRoads, `BAB`, `DR_Rd_Type`, KK_Alpl car lanes, `xroad4l` |
   | 3.5 m | `xroad2l` (paths ±1.75) |
   | 3.65 and 3.9 m (about 12 ft) | ScaleRail Rural, Town, Urban, Hiway, Dirt, Country (±1.95, ±5.6); `SLW_UR_`, `SLW_NC_`, `SLW_SB_`, `SLW_UX_`, `SLW_NX_`; `RR_Rural_` |
   | 3.7 m | `RR_Int_*` (±4.21, ±7.91); KK_Alpl's tram tracks (±1.85) |
   | 3.72 m | `xhwy` (±1.86; ±3.54, ±7.26) |
   | 3.353 m (11 ft) | `SLW_FW_` (±7.01, ±10.363, ±13.716) |
   | wider outer lanes | `SR_City_`, `SR_Parkway_` (±3.939, ±7.589) |
   | 6.8 / 11.8 m between two paths | `SR_Access_` (meaning unconfirmed) |

   Gaps between adjacent parallel paths that are not lanes appear in 764
   plain shapes. They hold different things: open terrain (fwy 4/6/8-lane:
   10 m and 4 m), a paved median or a centre turn lane without a path (`Fwy`,
   the NewRoads intersection kits and CLT pieces, 3 m), steel barriers and a
   hedge (`BAB` 3 m), a raised kerbed median (`SR_Parkway_` 3.5 m), grass
   (`Road4ldblStrt250m` 6 m, `RR_Int_*` 4.7 or 15.9 m, `SR_Inter_` 13.8 m,
   `SLW_FW_` 10.7 m), a tram reservation (KK_Alpl). The paths alone cannot
   tell which.

3. **Look.** Most sets are one flat textured strip at y = 0.1 m. Masters
   supplied new shapes for the default sets that "have been reset to the
   standard road height of 0.1m" (Notes.doc). All markings are
   in the texture, which spans the whole width (u 0..1) and repeats along the
   road: every 5 m for the 2-lane textures (`road2lane.ace`, `road.ace`,
   `road2lane_dt.ace`, `cr2l2wnanp.ace`, `hwy2l2wnanp.ace`), every 10 m for
   4-lane, brick and Urban4L, 20 m gravel, 14.28 m city, 80 m runway. The
   highway and freeway sets add 2 m shoulders falling from 0.1 to 0 m in their
   own texture (2.5 m repeat). Only BAB (and its PSK folder variants), the
   PSK `DS/` reskins, KK_Alpl and ScaleRail have 3D parts.

   Such a road is reproduced exactly by one polyline at y 0.1 m, u 0..1
   across the width, the same texture name and the same repeat
   (`DeltaTexCoord ( 0 0.2 )` for 5 m). A profile that names the texture this
   way keeps the route's own texture. PRUDNIK replaces many of these files
   (see 2.0 item 7).

4. **What TSRE does with road shapes today** (`TrackObj::useProceduralShape`,
   `ProceduralTrackPolicy::resolve`,
   `OrtsTrackProfileCatalog::profilesForPaths`):
   - A road TrackObj is procedural only with its own `ShapeTemplate`, or in
     `Forced` when the route has a ROAD profile named `DefaultTrack`. The
     policy looks up that name. `RdProfile`, the conventional default road
     family, is used only by the editor for new flex roads
     (`ObjTools`, `Tile.cpp`), not by this policy. The texture guess runs for
     rail only. So in
     `Forced` PRUDNIK's 13,056 road objects stay static; the filter file's
     `Kind ( Road )` is the way in.
   - Each path is swept on its own. The roles are set by listing order and
     start heading only: in a group of same-heading paths, the first is
     `LEFT`, the last `RIGHT`, the rest `MIDDLE`. The lateral offset and the
     spacing are not used.

   What goes wrong with that rule:
   - **Lane width** is fixed by the profile (assets-gen `PL_Street_2L` and
     `PL_Rural_2L` use 3.0 m), while families use 3.0-3.9 m.
   - **Medians:** the lanes beside a median become `MIDDLE`. The median
     stays empty, and the median-side edge of the carriageway has no edge
     line, barrier or kerb.
   - **Order:** 82 road shapes list same-heading paths out of left-to-right
     order. 19 of them are plain: 18 `DR_Rd_Type` straights such as
     `DR_Rd_Type3_10m.s` (paths -3, 3, -6, 6) and `SLW_7thSt_2LS_012m.S`
     (1.95, -1.95). Their roadside would be drawn in the middle of the road.
   - **Definition errors:** 16 curves have lane paths that do not stay
     parallel. `US1Rd4l*r10d/20d` (8, Kuju) give the ±4.5 m lanes the radii
     of ±6 m; Masters' notes say the 4-lane curves "have bad trackshape
     definitions". `SR_Rural_4LCrv_3000r01d`, `4000r01d`, `4000r05d` and
     `SR_Parkway_4LCrv_0500r01d` give one lane a wrong radius or angle.
     `KK_Alpl6l_290r*` (4 here, 14 PRUDNIK objects) use two radii for six
     lanes. Swept per lane, these show gaps and overlaps.
   - **Lane add/drop:** two paths start at one point (`BAB2L1W_to_3L_R_60m`:
     paths -1.5, 1.5, 1.5). They become `MIDDLE` and `RIGHT` and overlap.
   - **Mirroring:** one-way roads have asymmetric markings. A curve shape
     placed the other way is the same shape reversed, so its markings swap
     sides. NewRoads therefore has `l`/`r` pairs: "For the fwy(1/2/3/4)l1wna*
     curve pieces the l/r on the end denotes which side the yellow line is
     on" (Notes.doc). The same goes for `hwy3l2wL`/`R` (the side of the third
     lane, unconfirmed). A profile swept on a reversed path is mirrored the
     same way. So the filter must choose mirrored members by the name's
     `l`/`r`.

   Suggestion (not in the design): a ROAD member that draws the whole
   cross-section once per group of parallel paths, stretched between the
   outermost paths. Its left part follows the leftmost path, its right part
   the rightmost, and the middle is spread. That is the rule the design gives
   the `COMPLEX` bed for track. Add lane lines placed between adjacent
   paths, and a median part where the gap between adjacent paths is more than
   a lane. One member then fits any lane count and any lane width of a
   family, draws lane add/drop tapers, and is immune to path order and to
   most definition errors (sweep along one reference path).

5. **Junction-like shapes.**

   | Class | Shapes | Examples |
   | --- | ---: | --- |
   | junction: paths on another axis or diverging | 296 | `Road2LaneT`, `Road2LCross`, `cr2l2wnaX20m45d4way`, `SR_City_2LXng_45d`, `prrw160mX60d`, `fwy2l1wnaStrt50mexit` |
   | lane change: spacing or end points change | 59 | `BAB2L1W_to_3L_R_60m`, `Fwy6LT_to_8LT_60m`, `pr2l2wnaI20m_Y`, `hwy2l2wna_CLTend_25m` |
   | definition error (plain by name) | 16 | `US1Rd4l250r20d`, `KK_Alpl6l_290r10d` |
   | intersection-kit pieces with plain paths | 164 | `hwy2l2wnaIS_CL_3m`, `pr4l2wnaICW_4L_3m`, `SR_City_2LStr_010m_T1`, `SR_Parkway_4L2LTee_Dummy` |
   | painted-marking pieces | 112 | stop line `*sl`, yield `*sl_alt`, crosswalk `*slx`, rail crossing `*_rr`/`*_RRX`, `*_PED`, `*_SCH`, `*_STP`, `*_SLO`, `*_STA`, `*_NS1/2`, no markings `*blank*` |
   | transitions to another road type | 8 | `hwy2l2wnaStrt5mxdr2l`, `cr2l2wnaStrt5mxgr2l`, `pr4l2wnaStrt10mxhwy4l`, `hwy2l2wnaStrt40m2lto4lext` |

   So 355 shapes (9.5 %) have paths that cross, diverge or change spacing.
   A swept profile cannot draw them. Another 284 (7.6 %) sweep correctly, but
   their look belongs to an intersection, a marking or a transition.

   What a road junction member would need (not in the design; the track
   `COMPLEX` member does not cover it):
   - a surface filling the union of all arms' carriageways, with the
     markings dropped inside the junction box;
   - corner fillets: kerb returns where the family has kerbs, shoulder corners
     on highways (NewRoads has a `hwynashldrcor.ace` texture for them);
   - stop lines, yield lines and crosswalks at the arm ends (`Place` at
     `PathStart`/`PathEnd`), centre decals;
   - gores and painted islands for exits, merges and turn lanes (the NewRoads
     kit pictures show hatched islands and turn arrows);
   - skew angles: 30, 45, 55, 60, 83, 85 and 90° occur.

   The outline cannot always come from the paths:
   - Kuju's T and X put the side road's paths from the main road's edge right
     across it. In `Road2LaneT.s` the side paths start at x = -3 and run 13 m
     to the right.
   - `dr2lY20m45d.s` and `gr2lY20m45d.s` are 20 m pieces made of six 2 m path
     stubs.
   - The NewRoads intersection kit builds a junction from a centre piece
     (`hwy2l2wnaStrt10m_X90d.s`, a 10 m straight with cross paths) and
     separate approach pieces with stop lines and shoulder transitions. See
     the kit picture `Pictures/Hwy2l_Intersection_Kit.jpg` in the RBK.

   The design's grouping rule ("paths that come closer than a tie length plus
   `TieGap`") mostly finds these: lanes are at least 3 m apart, while
   crossing and splitting lanes come closer. But a ROAD family has no tie
   length. Roads need their own threshold, or "paths on another axis,
   diverging, or changing spacing".

6. **Filter patterns.** Matching ignores case, and that merges sets:
   - `fwy*` also matches `Fwy*`;
   - `dr*` matches `DR_*` (Bjorn van Steenbergen's track and roads);
   - `pr*` matches `prrw*` (runways);
   - `br*` matches PSK's folder `Br_BAB_2w/...`;
   - `*Plat*` matches rail names.

   Use `Kind ( Road )` with longer patterns: `fwy?l?w*`, `Fwy?LT*`, `dr2l*`,
   `pr?l*`, `br2l*`, `*Plat*`. `_NA` variants have no dynamic shadows ("_NA
   (=No Alphasorting)", NewRoads guide). They look the same, so they take the
   same profile as their base shape.

7. **PRUDNIK** (13,056 road objects):
   - **Layout:** 12,045 objects (92 %) use the plain 2-lane layout, paths at
     ±1.5 m, a 6 m road. 12,835 (98.3 %) are plain: no junction, lane
     change, kit, marking or transition piece. Junction-like: 149
     objects of 15 junction shapes (`JP2rd2LaneT` 61, `Road2LaneT` 41,
     `br2l2wnaT20m90dbr2l` 11, `Road2LCross` 10, `JP2rd2Lcrs` 8,
     `DirtRoad2LaneT` 5, ...). Then 47 lane-change objects
     (`pr2l2wnaI20m_Y` 30, BAB 9, hwy 8) and 16 on definition-error curves
     (KK_Alpl 14, US 2).
   - **By geometry family:** JP 6,993; Road 1,387; DirtRoad 1,199; br 949;
     dr 653; gr 576; Urban 424; cr 343; BAB 225; hwy 134; KK_Alpl 76;
     fwy 53; pr 30; US 7; Fwy 7.
   - **Renamed files:** 235 objects use another file than their geometry's
     shape. 156 `cr2l2wna*np.s` sit on JP2Rd geometry, 23 on br and 4 on dr;
     there are 22 BAB folder variants (`BAB_2w_emb/BAB_emb_*`,
     `BAB_2w_br/BAB_br_*`), 14 `DS/DS1*_...` reskins on JP, US and br
     geometry, and 10 `br2l2wna*` files on JP geometry. So the look comes
     from `FileName`, as the design's filters assume.
   - **Textures:** PRUDNIK's `TEXTURES` replace the American textures
     (viewed as PNG):
     - `road.ace` is the same file as `road2lane.ace`: worn asphalt with a
       white dashed centre line. JP ("paved roads, no lines" in NewRoads)
       and Road2L look the same.
     - `cr2l2wnanp.ace`: unmarked worn asphalt.
     - `road2lane_dt.ace`: a dirt track with grass.
     - `road2lane_gr.ace`: patched concrete.
     - `brickroad.ace`: cobblestones with edging.
     - `Urban4Lp.ace`: dark asphalt with three dashed lane lines.
     - `hwy2l2wnanp.ace` keeps the US double yellow line.

     The objects on JP, Road and US geometry that keep their own file
     names (about 8,200) thus share one look. The 156 unmarked
     `cr2l2wna*np.s` files on JP geometry have another.
   - **Filter sketch.** The profile names other than `PL_Rural_2L` and
     `PL_Street_2L` are hypothetical:

     ```text
     Filter ( Kind ( Road ) Shapes ( "KK_Alpl*" ) Profile ( DISABLED ) )
     Filter ( Kind ( Road ) Shapes ( "*LaneT*" "*2Lcrs*" "*LCross*" "*T20m90d*" "*_T90d*" "*I20m_Y*" "*I40m_Y*" ) Profile ( DISABLED ) )
     Filter ( Kind ( Road ) Shapes ( "BAB*" ) Profile ( PL_Motorway ) )
     Filter ( Kind ( Road ) Shapes ( "DS/*" ) Profile ( PL_Road_GuardRails ) )
     Filter ( Kind ( Road ) Shapes ( "br2l*" ) Profile ( PL_Cobble_2L ) )
     Filter ( Kind ( Road ) Shapes ( "DirtRoad*" "dr2l*" ) Profile ( PL_Dirt ) )
     Filter ( Kind ( Road ) Shapes ( "gr?l*" ) Profile ( PL_Gravel ) )
     Filter ( Kind ( Road ) Shapes ( "cr2l*" ) Profile ( PL_Asphalt_Unmarked_2L ) )
     Filter ( Kind ( Road ) Shapes ( "Urban*" ) Profile ( PL_Street_Multi ) )
     Filter ( Kind ( Road ) Shapes ( "JP*" "Road*" "US1Rd*" ) Profile ( PL_Rural_2L ) )
     ```

     Gravel has a 1-lane (3 m, one path) and a 2-lane member. BAB needs the
     median member (item 4) before it can leave `DISABLED`.

#### 2.1 Kuju default roads with NewRoads additions (`Road`, `DirtRoad`, `JP`, `US`)

- **Source:**
  - The originals are Kuju's (`_INFO` "Original MS/KUJU Road Shapes"); 77
    are in `MSTS/GLOBAL/SHAPES`.
  - Steven Masters extended them in NewRoads. He added straights 5-200 m,
    curves of 5/10/20° and `_NA` copies, plus "Straight 1 Lane Sections",
    "Straight 6 Lane Sections", "Straight 4 Lane Sections With Grass Lane" and
    "Terrain Levelers - Ficticious Roads" (`Road10Lstrt500m`/`1000m`, 30 m
    wide).
  - Notes.doc lists the JP set as "Paved Roads, (No Lines). This is the
    default JP2 road set. Bruce Bridges added a tight 90d turn"
    (`JP2Rd3r90d`). The default `Road2L`/`Road4L` sets are the base of
    NewRoads' "Urban Roads (2-way)".
  - `JP` is by name the road set of Kuju's Japanese routes, and `US1Rd` that
    of the US routes (unconfirmed, from names).
  - Confidence: high (tsection credits, RBK Notes.doc).
- **Shapes:**

  | Prefix | B54 / PSK | Local | PRUDNIK |
  | --- | --- | --- | ---: |
  | Road | 61 / 49 | 49 | 1,387 |
  | DirtRoad | 22 / 22 | 22 | 1,199 |
  | JP | 49 / 49 | 49 | 6,993 |
  | US | 24 / 24 | 24 | 7 |

- **Kind:** road; gauge field 1.5 (meaningless).
- **Contents** (156 shapes):
  - paths: 1 path 7, 2 paths 91, 4 paths 48, 6/8/10 paths 10;
  - straights, 40 m radius curves of 22.5/45°, 250-1500 m curves (JP, US);
  - 12 junctions: `Road2LaneT`, `Road4LaneT`, `Road2LCross`, `Road4LCross`,
    `Road4to2LnT`, the same in DirtRoad, `JP2rd2LaneT`, `JP2rd2Lcrs`;
  - 8 defective 4-lane US curves.
- **Look** (looked at `Road2LStrt10m`, `Road4LStrt10m`, `JP2Rd10mStrt`,
  `DirtRoad2LStrt10m`, `US1Rd2l1000r5d`, `US1Rd4l1000r10d`, `Road2LaneT`,
  `Road2LCross`, `JP2rd2LaneT`):
  - one flat quad strip at y 0.1 m, 6 m (2-lane) or 12 m (4-lane) wide;
  - `road2lane.ace` / `road4lane.ace` / `road.ace` / `road2lane_dt.ace`
    repeated every 5 or 10 m;
  - the Road and DirtRoad T and X have a separate centre square texture
    (`road2LaneT.ace`, `road2Lcross.ace`); JP's use `road.ace` throughout;
  - Notes.doc: dirt roads are "6m(2-lane) or 12m(4-lane) width with no
    shoulder".
- **Profile needs:**
  - ROAD MAIN = one polyline -3..3 (or -6..6) at 0.1 m with the set's texture;
  - LEFT/RIGHT (MIDDLE) = 3 m lanes taking u 0..0.5 and 0.5..1 (quarters for
    4-lane) of the same texture, so the markings stay right;
  - filters `JP*`, `Road*`, `DirtRoad*`, `US1Rd*`;
  - junctions (12) need the road junction member, or stay static;
  - the US 4-lane curves need a sweep along one reference path;
  - `Road10L*` "terrain levelers" probably want an invisible or ground
    profile (their purpose is from the `_INFO` only).
- **Notes:** this family carries 73 % of PRUDNIK's road objects.

#### 2.2 NewRoads local roads (`cr`, `pr`, `Urban`, `br`, `dr`, `gr`, `rd`, `prrw`)

- **Source:** NewRoads, by Steven Masters with pieces by others. The
  installation guide lists the NewRoads 4.0 authors as Bruce Bridges, Ted
  Curphey, Martyn T. Griffin, Steven Masters, Jeff Rice and Norbert Rieger,
  and says version 4.00 includes tsection build 38. RBK 3.2 (2004) is "by
  Bruce Bridges, Martyn T. Griffin, Steven Masters, Jeff Rice". From
  Notes.doc and the readmes:
  - `cr` Country Roads: "basic 6m width with no shoulder"; pass (`*p.s`) and
    no-pass (`*np.s`), stop-line (`sl`), yield (`sl_alt`) and rail-crossing
    paint (`_rr`) pieces, transitions to dirt and gravel (`xdr2l`, `xgr2l`).
  - `pr` Urban Roads (2-way): Masters expanded the default 2- and 4-lane
    sets (textures `prna*`, `road2lane`, `road4lane`); intersection kits.
  - `Urban` Urban Roads (1-way): "3 new sets of urban roads set up for
    one-way travel (by default). These were requested by Bruce Bridges."
    2, 3 and 4 lanes.
  - `br` Brick Roads: "brick (cobblestone) urban roads 6m wide with no
    shoulders".
  - `dr` dirt roads: Kuju's plus additions by Jeff Rice (`_INFO` "Dirt
    Roadway Shapes from Jeff Rice") and Masters.
  - `gr` Gravel Roads by Martyn T. Griffin, 2002: "clones from the standard
    MSTS road shapes and Dirt road shapes", 3 m (1-lane) or 6 m (2-lane).
  - `rd` Residential City Roads by Griffin: "Asphalt road 6 meters wide (2
    lanes) with cement curbs ... sidewalks that are just over 1 meter wide,
    and a 2 1/2 meter wide outlawn (boulevard) ... The outlawn area is
    transparent".
  - `prrw` Runway: Bruce Bridges' two straights for his Conrail Indianapolis
    route, plus Masters' end and 60° cross; "All of these pieces are 48m
    wide".
  - Ted Curphey made the 45° intersections for `cr`, `dr` and `gr` (`_INFO`).

  Confidence: high.

  Packages today: the NewRoads 4.0 end-user installer is on trainsim.com
  (fid 6254, `UKTS_6254_NewRoads_V40_MSTS1_Setup.exe.zip`, Steven Masters,
  2007-11-23, "Autoinstaller provided by Yuri J. Sos"; also fid 69884), with
  a files-only copy (fid 80272, 2014), v3.2 (fid 68002) and the Route
  Builders Kits (fid 60104 North America; fids 60105 and 6256 UK; also on
  Steam4me). Steam4me's own link to the 4.0 installer points to
  railpage.com.au, which does not connect now. the-train.de lists a
  "NewRoads v4.0 Grundversion"
  (<https://the-train.de/downloads/entry/7912-newroads-v4-0-grundversion>,
  not opened).
- **Shapes:**

  | Prefix | B54 / PSK | Local | PRUDNIK |
  | --- | --- | --- | ---: |
  | cr | 171 / 171 | 171 | 343 |
  | pr | 106 / 106 | 106 | 30 |
  | Urban | 158 / 158 | 156 | 424 |
  | br | 26 / 26 | 26 | 949 |
  | dr | 70 / 70 | 70 | 653 |
  | gr | 42 / 42 | 42 | 576 |
  | rd | 12 / 12 | 12 | 0 |
  | prrw | 4 / 4 | 4 | 0 |
  | **total** | **589 / 589** | **587** | **2,975** |

- **Kind:** road; gauge field 1.5.
- **Contents** (589 shapes):
  - paths: 1 path 14, 2 paths 361, 3 paths 55, 4 paths 142, 6-8 paths 17;
  - 74 junctions: `cr` T/X/Y 20 m 45° with 2way/3way/4way variants,
    T/X 85°/90° centre pieces; `pr` intersection centres
    `IC_T2x2_90d_6m`...`IC_X4x4_90d_12m`; `gr`/`dr` T/X/Y; `br` T/X 90°;
    `rd` intersection and T; the runway cross;
  - 3 lane changes (`pr2l2wnaI20m_Y` and `pr4l2wnaI20m_Y`, the start of a
    divided road; `pr2l2wnaStrt40m2lto4l`);
  - 45 kit pieces with plain paths: `pr` `IS_CL`, `IS_NCL`, `ICW`, `ICS`,
    `I_A` curves, and the court dead end `rd2lctcty`. CL / NCL are probably
    "centre lane" / "no centre lane": those pieces have paths at ±3 or
    ±3/±6 with a 3 m pathless centre lane (unconfirmed);
  - 39 marking pieces;
  - 5 transitions.
- **Look** (looked at `cr2l2wnaStrt10mnp`, `br2l2wnaStrt10m`, `dr2lStrt20m`,
  `gr2l50mStrt`, `gr1l10mStrt`, `Urban4LStrt20m`, `rd2lstrtcty`,
  `rd2lintcty`, `prrw80mStrt`, `pr2l2wnaI20m_Y`, `pr2l2wnaIS_CL_3m`,
  `cr2l2wnaX20m45d4way`):
  - every one is a flat strip at y 0.1: 3 m (gr1l), 6 m, 9 m (Urban3L),
    12 m, 14.28 m (`rd`: kerbs, sidewalk and lawn painted in one texture,
    lawn transparent), 48 m (runway);
  - `pr2l2wnaI20m_Y` widens to 9 m around a filler island; PRUDNIK's
    texture makes it grass.
- **Profile needs:**
  - ROAD MAIN one polyline per set with its texture;
  - side roles as in 2.1;
  - `Urban` is one-way: lane lines only, no centre line; `cr`/`pr`
    pass/no-pass pairs differ only in texture (filters `*np.s` / `*p.s`);
  - `rd` needs an alpha-tested texture (transparent lawn) or real kerb and
    sidewalk polylines;
  - runway end (`prrw80mend`) = straight + end decal;
  - the 74 junctions and 3 lane changes need the junction member; the 45 kit
    pieces and 39 marking pieces need texture variants by filter or decals;
  - filters `cr2l*`, `pr?l*` (not `pr*`), `Urban*`, `br2l*`, `dr2l*`,
    `gr?l*`, `rd2l*`, `prrw*`.
- **Notes:** PRUDNIK uses `br`, `dr`, `gr`, `Urban` and `cr` heavily (2,945
  objects) and `pr` only as `pr2l2wnaI20m_Y` (30, a lane change).

#### 2.3 NewRoads highways and freeways (`hwy`, `fwy`, `Fwy`)

- **Source:** Steven Masters, NewRoads ("Highway and Country Road Shapes from
  Steven Masters", "Roadway Shapes From Steven Masters Roadway"). Ted Curphey
  made the CLT pieces (`_INFO` "Road Shapes from Ted Curphey, 2lane CLT's
  straight", "CLT intersections", "hwy 2lane 45d intersections"). Jeffrey
  Kraus-Yao made 10 `hwy2l2wnaStrt*msp` straights (`_INFO` "Road shapes by
  Jeffrey Kraus-Yao"; "sp" unconfirmed). Notes.doc:
  - **Highways:** "a large collection of 2, 3, and 4 lane roads. The width
    for these roads are as follows: 2-lane 10m, 3-lane 13m, and 4-lane 16m.
    All these sections have a 2m wide shoulder on both sides that have been
    tapered to ground level". Full intersection pieces, an intersection kit,
    pass/no-pass, and "several left-side running (UK) road sections" (`uk`
    in the name instead of `na`).
  - **Freeways:** "1, 4, 6, and 8 lane freeways. All these sections have a
    2m wide shoulder on both sides ... These sections come in both divided
    (each side is separate) and full (both sides attached) sections". Plus
    "a Divided Highway intersection kit ... for using the 4-lane freeway
    sections as a divided highway".
  - **`Fwy`** (capital F, `Fwy6LT`/`7LT`/`8LT`, 128 shapes, `_INFO` "Shapes
    From Steven Masters Roadway") is not in Notes.doc 3.2. Its lane layout
    equals BAB's `?LT` layout. Whether it came with NewRoads 4.0 is
    unconfirmed.

  CLT = centre left-turn lane: the RBK picture `Center_Left_T.jpg` shows a
  two-way turn lane with arrows (the name's meaning is from that picture).
  Confidence: high for hwy/fwy, medium for Fwy.
- **Shapes:**

  | Prefix | B54 / PSK | Local | PRUDNIK |
  | --- | --- | --- | ---: |
  | hwy | 560 / 550 | 546 | 134 |
  | fwy | 464 / 464 | 464 | 53 |
  | Fwy | 128 / 128 | 128 | 7 |

- **Kind:** road; gauge field 1.5.
- **Contents** (1,152 shapes; paths 1-10):
  - `hwy`: 2/3/4 lanes, 3-lane `L`/`R` pairs, curves 40-771.5 m (sets of
    concentric radii such as 748.5/750/770/771.5 for parallel roads);
    - 54 junctions: T/X/Y 30 m 45°, `T90d`/`X90d` centre pieces, turn
      lanes, CLT T/X;
    - 19 lane changes: `CLTend`, `I40m_Y`, `Merge`, `Exit`, `EM`,
      `3lto2lw`;
    - 108 intersection-kit pieces (`IS_*`, `I_*`, `ukI_CRS*`);
    - 42 marking pieces (CLT straights and curves, `sl`, `blank`,
      `_RR_1R`/`2R`).
  - `fwy`: one-way carriageways of 1-4 lanes (`fwy?l1w`), each in `l`/`r`
    curve pairs (130 + 130), and full 4/6/8-lane roads (`fwy?l2w`) with an
    open median: inner lane paths ±8.5 m (4 and 6 lanes) or ±5.5 m (8
    lanes), leaving 10 m or 4 m of terrain. Also 10 junctions (exits, merges,
    the divided-highway T/X with cross paths at x = -8), 8 lane changes and
    9 marking pieces (`wl` white line, `sl`).
  - `Fwy`: two carriageways joined by a 3 m paved median (`fwyfiller.ace`),
    lanes 3 m, 2 m shoulders; 9 junctions and 5 lane changes (`_E_to_`,
    `_M_to_`, `_EM_to_`, `_to_6LW`, `_to_8LT`).
- **Look** (looked at `hwy2l2wnaStrt10mnp`, `hwy4l2wnaStrt10m`,
  `hwy2l2wnaStrt10m_T90d`, `hwy2l2wnaStrt10m_X90d`, `fwy2l1wnaStrt10m`,
  `fwy4l2wnaStrt10m`, `Fwy8LTS020m`):
  - a flat carriageway at 0.1 m plus shoulder strips falling to 0 at the
    outer edge;
  - US markings in the texture: double yellow centre, white edges, yellow
    median-side edge on one-way carriageways;
  - the intersection centre pieces use separate textures (`hwy2l2wnaT.ace`,
    `hwynaap.ace`, `hwynashldrcor.ace`).
- **Profile needs:**
  - ROAD members with carriageway + outer shoulders (shoulder only on
    `LEFT`'s left and `RIGHT`'s right);
  - one-way members with a median-side edge line, mirrored for the `l`/`r`
    pairs;
  - a median part for `fwy?l2w` (open terrain: draw nothing, but the inner
    shoulders) and `Fwy` (3 m paved);
  - UK variants (`uk`) are left-side running, so the markings are mirrored;
  - 105 junction-like shapes, 108 kit pieces and 51 marking pieces are the
    largest junction workload of any family;
  - filters `hwy?l?w*`, `fwy?l?w*`, `Fwy?LT*`.
- **Notes:** PRUDNIK uses 194 objects, mostly plain (hwy 2- and 3-lane
  straights and curves, `fwy2l1w` and `fwy1l1w` curves).

#### 2.4 German Autobahn by Norbert Rieger (`BAB`)

- **Source:**
  - `_INFO` "Shapes From Norbert Rieger German Autobahn" (406). BAB =
    Bundesautobahn.
  - The developer list names Rieger for "XTracks Additions". BAB is part
    of NewRoads 4.0: DBTracks' Autobahn page lists "NewRoads4_BAB_Shapes.rar"
    (406 pieces, 08.09.2007) as "all BAB Shapes of NewRoads v4.0
    (Standard), registered in tsection.dat #38+", the same count as the
    `BAB` shapes here (<https://dbtracks.com/pages/DL_bab.html>). RBK 3.2
    does not have it.
  - The same page lists Rieger's later variants (2010, "partially
    Laboratory or beta status"; 2,284 shapes in all): one-carriageway
    (`BAB_1w_*`, 562 pieces each) and two-carriageway (`BAB_2w_*`, 288
    each) roads and bridges, embankment ends, bridge piers, and
    `BAB_Exit_Merge` (25 entrances and exits "from NewRoads 4.0") with
    `BAB_Facts.rar` ("Instructions and description of the entrances and
    exits"; not opened). "The usage in a route intended for publication
    should be discussed with the author". The page also notes: "Currently
    there are not all '1L1W' objects available in Newroads' BAB selection.
    Only Newroads' 'fwy1l1w' objects are valid for 'track shape
    substitution'."
  - DBTracks 1.0 also offered "BAB_Standard.rar" (2011, 825 pieces):
    one-direction carriageways "to replace 2L and 4L road sections from the
    MSTS basic selection" (<https://dbtracks.com/pages/DBTracks10.html>).
    PSK's `GLOBAL/SHAPES` holds such reskins of Kuju geometry by name
    (`BAB1w2L_cgr_Road2Lstrt100m.s`, `BAB1w3L_br_Road4Lstrt100m.s`, ...;
    `cgr`, `cgrb`, `i`, `n` unexplained).
  - Packages today: NewRoads 4.0 (2.2); Rieger's variants on the-train.de
    (closing, see "Use today"); the BAB textures on ActivitySimulatorWorld
    (`Newroads4_BAB_textures.zip`, 2009,
    <https://www.activitysimulatorworld.net/Les_utilitaires/NewRoads_V4.0_Autobahn_Texturen.htm>).
  - Use: CMK and PRUDNIK; non-German routes ship `bab2l1w*` (Gotland fid
    78383, Dorset Coast v6 fid 88951, SRv2 Phase 3 fid 85961, Oro fid
    91223); German routes were not identified. Picture:
    `/home/arch/tsection-families-web/pictures/road-bab/` (1, the BAB
    textures' preview, medium confidence).
- **Shapes:** 406 / 406, all local in OR_PSK_NEW and tsdata.
  - OR_PSK_NEW also has five folders of 288 shapes each, all with the same
    two-way geometry: `BAB_2w_1g`, `BAB_2w_emb` (embankment), `BAB_2w_br`
    (bridge deck), `BAB_2w_ramp` and `Br_BAB_2w` (bridge structure).
    These are Rieger's DBTracks variants: the folder names and the count
    288 match the archives on the DBTracks page, which explains them
    (`1w`/`2w` = one or two carriageways):
    - `2w_1g`: "2 Lanes, center guardrail, hard shoulder on both sides,
      center bush row (Standard)";
    - `2w_emb` and `2w_ramp`: the same on an embankment or a ramp
      ("guardrail and hard shoulder on both sides, bush row center");
    - `2w_br`: "2 Lanes for bridge, guardrail and hard shoulder on both
      sides"; `Br_BAB_2w`: "Bridges for 2 way roads";
    - not in PSK: `2w_2g` "guardrail on both sides"; for one carriageway
      `1g` "guardrail left, hard shoulder right (Standard)", `2g`
      "guardrail on both sides", `B` "bush row left".
  - Each folder holds 144 shapes and 144 `oS` variants (`BAB_emb_*` and
    `BAB_oS_emb_*`, ...): 48 per lane count (4LT, 6LT, 8LT). "oS" is
    probably "ohne Standstreifen", without hard shoulder (unconfirmed; the
    DBTracks page does not explain it).
  - PRUDNIK: 225 objects, 22 of them folder variants (`BAB_2w_emb` 18,
    `BAB_2w_br` 4).
- **Kind:** road; gauge field 1.5.
- **Contents:**
  - `BAB?L1W` is one carriageway of 2-5 lanes, 3 m lanes (paths -1.5..1.5,
    -3..3, ...), with straights (`S020m`, ...), curves and `GR`/`EM`
    variants (unconfirmed meaning).
  - `BAB4LT`/`6LT`/`8LT` are both carriageways (lane paths ±3, ±6, ±9, ±12;
    3 m central reservation).
  - 6 junctions and 10 lane changes: lane add/drop pieces `_E_to_`,
    `_M_to_`, `_to_?L_L`/`_R`/`_E`/`_M`. One path splits from another at a
    point, or ends at another heading.
- **Look** (looked at `BAB4LTS020m`, `BAB4LTS050m`, `BAB2L1WS020m`,
  `BAB_2w_emb/BAB_emb_4LTS050m`, `BAB_2w_br/BAB_br_6LTS060m`,
  `BAB_2w_ramp/BAB_oS_ramp_4LTS050m`, `Br_BAB_2w/Br_BAB4LTS050m`):
  - real 3D;
  - `BABtex01`: asphalt lanes with white dashed lines, a lighter hard
    shoulder, grass verge, and an embankment down to -0.85 m (to -16.3 m in
    `BAB_emb`);
  - `BABtex02`: double-sided steel guard rails on posts in the central
    reservation (0.5-0.8 m) and a hedge strip up to 2 m;
  - the one-carriageway pieces have the barrier on the left (median) side
    and the hard shoulder on the right;
  - 4LT is 25 m wide (x ±12.5), 4-lane emb 57 m.
- **Profile needs:**
  - ROAD members: carriageway, hard shoulder, verge and embankment slope
    (polylines);
  - a median member with Template3D guard rails (posts `Repeat`, beam
    `Sweep`) and a hedge (alpha-tested strip);
  - side roles do not work for `?LT` (median between `MIDDLE` lanes; item 4);
  - the folder variants need their own families by folder name
    (`BAB_2w_emb/*`, `BAB_2w_br/*`, `Br_BAB_2w/*` = bridge structure, which
    is closer to a STATIC or bridge profile);
  - lane add/drop pieces need the stretched member or the junction member;
  - real Autobahn lanes are 3.75 m; the set uses 3 m, so a profile must
    follow the paths, not the prototype.

#### 2.5 ScaleRail road pieces (`SR_*` road prefixes)

- **Source:** 3DTrains ScaleRail (`_INFO` "Track/Road Sections from 3DTrains
  ScaleRail Products"; developer list "Marc Nelson (Scale Rail)"). Set
  identity: see the rail section.
  - "ScaleRail also includes ScaleRoad - a system of city, urban, highway,
    and other road sections. Like ScaleRail, ScaleRoad supports dynamic
    shadows" (3DTrains, 2006,
    <https://web.archive.org/web/20060822005749/http://www.3dtrains.com:80/downloads/scalerail/>).
    Versions 1.74 (by December 2008), 1.75 (2010), 1.77 (about 2013). The
    LaSalle Rockford & Central route needed shapes missing from 1.74
    (`sr_dirt_*`, `sr_inter_*`, `sr_rural_*`; 3DTrains topic 15872,
    <https://web.archive.org/web/20210728160824/https://www.3dtrains.com/forums/index.php/topic/15872-lsrc-route-scale-road-file-errors/>);
    its `lsrc_glo.zip` (fid 71784, 2008) "includes Scaleroads shapes that
    were available in previous releases of Scaleroads but are no longer
    available".
  - Packages: "Scaleroad by 3DTrains" (fid 87684, 2023, "Uploaded with
    permission from 3DTrains"; holds `ScaleRoad.exe`) and Elvas Tower file
    3208 (Genma Saotome, 2022-23, "present here on a temporary basis
    pending a return to distribution by 3dtrains";
    <https://www.elvastower.com/forums/index.php?/files/file/3208-scale-roads/>).
  - Textures: the installers put the shapes into `GLOBAL/SHAPES`; the
    textures belong in each route's `TEXTURES`, so routes ship them ("the
    textures come with the Route", The Siskiyou Route,
    <https://web.archive.org/web/20200502131348/http://www.siskurail.org/installingScaleRail.php>).
    "MSTS Route--Saluda Road Textures" (fid 69357, 2007: "ScaleRoad texture
    files ... Textures created by 3DTrains") holds 45 `SR_Road_*.ace` and 22
    in `SNOW/`, for Access, City (also Green, Red, Yellow), Country, Dirt,
    Hiway, Rural and Urban. `Town`, `Inter` and `Parkway` have no texture
    of their own name there (they probably reuse others; not checked).
    Whether `ScaleRoad.exe` installs textures too was not checked. This
    fits TS_STARTER_ROUTE having the `SR_*` meshes and no `SR_Road_*.ace`.
  - Not the same set: 3DTrains' "ScaleRoad - High Resolution Edition"
    (2009) is for EA Rail Simulator.
  - Pictures: `/home/arch/tsection-families-web/pictures/road-scalerail/`
    (2: the Elvas Tower download's Open Rails screenshot of a 2-lane road; a
    2005 3DTrains picture with a 4-lane road).
- **Shapes:** 739 / 0 (Build 54 only).

  | Prefix | Shapes | Local (TS_STARTER_ROUTE) |
  | --- | ---: | ---: |
  | SR_Rural_ | 124 | 124 |
  | SR_Town_ | 108 | 106 |
  | SR_City_ | 103 | 96 |
  | SR_Urban_ | 86 | 80 |
  | SR_Dirt_ | 80 | 80 |
  | SR_Hiway_ | 79 | 79 |
  | SR_Inter_ | 60 | 60 |
  | SR_Access_ | 43 | 0 |
  | SR_Country_ | 32 | 32 |
  | SR_Parkway_ | 24 | 24 |

  681 are local. Their `SR_Road_*.ace` textures are not local (they come
  with routes; see Source). PRUDNIK: 0.
- **Kind:** road pieces of a track-and-road set; gauge field 1.5 (three
  SR_Dirt sections 1.435).
- **Contents:**
  - paths: 1 path 28, 2 paths 404, 3 paths 6, 4 paths 278, 6-8 paths 23;
  - 73 junctions: `Xng` crossings at 30/45/60°, `Tee`, `Turn` (`LR`/`RR`),
    `Intersection`, `IntShort`, `Mrg`;
  - 2 lane changes, 4 definition errors;
  - 11 kit pieces (`_T1`/`_T2` straights, `Tee_Dummy`, `Xng_Dummy`, `Corner`);
  - 18 marking pieces (`_PED`, `_RRX`, `_SCH`, `_SLO`, `_STA`, `_STP`,
    `_NS1`/`_NS2`; meanings from names, unconfirmed).
- **Look** (meshes only; looked at the 20 m or 85 m straight of City, Town,
  Urban, Rural, Hiway, Inter, Country, Dirt and Parkway, and the Rural and
  City 2-lane intersections):
  - City: 14.64 m road with 0.15 m kerbs and sidewalks to 20.8 m;
  - Town: 11 m road, sidewalks to 17.1 m;
  - Urban: 11 m road, sidewalks to 14.1 m;
  - Rural: 11 m flat;
  - Hiway: 11 m with guard rails 0.8 m high both sides;
  - Country: guard rail on one side;
  - Dirt: 10 m;
  - Inter: 36 m divided, median in the texture;
  - Parkway: 19.6 m with kerbs and a 3.5 m median raised 0.3 m;
  - the City intersection has chamfered corners.
- **Profile needs:**
  - families with 3.65/3.9 m lanes;
  - kerb (0.15 m vertical face) and sidewalk polylines;
  - guard rails (Template3D) on one or both sides (Country is asymmetric, so
    it is mirrored when reversed);
  - medians (Inter, Parkway raised);
  - the junction member for 75 shapes;
  - texture variants or decals for the 18 marking pieces;
  - filters `SR_City_*` etc. with `Kind ( Road )`; `SR_*` alone also takes
    ScaleRail track.

#### 2.6 J.A. Norton roads (`SLW_*`)

- **Source:**
  - `_INFO` "J.A. Norton Road sections" and "Sections From J.A. Norton
    Roadway/Track"; the developer list: "J A Norton ElvasTower".
  - The set's name and what "SLW" stands for are unconfirmed (web search
    found nothing in either round; no file on Elvas Tower's guest-visible
    library or on trainsim.com). The lane widths are US (11 and 12 ft), and
    `SLW_7thSt` names a "7th Street".
- **Shapes:** 245 / 0 (`SLW_UR_` 144, `SLW_NC_` 44, `SLW_SB_` 26, `SLW_FW_`
  15, `SLW_` 5, `SLW_UX_` 4, `SLW_NX_` 4, `SLW` 2, `SLW_UR_SB_` 1). None
  local. PRUDNIK: 0.
- **Kind:** road; gauge field 1.5.
- **Contents:**
  - paths: 2 paths 122, 4 paths 58, 6 paths 46, others 19;
  - `UR`/`NC`/`SB` look like street types (meaning unconfirmed), with 2-,
    4- and 6-lane straights (`LS`) and curves (`LC`);
  - 43 junctions: `2L-2LX_90d`, `4L-6LX_90d`, `2L-4LT_55d`, `M_NNd` merges,
    `X_End`, `Parker_X`, `_Core`. Cross-street paths often start 20 m to the
    left (x = -20);
  - 3 lane changes;
  - `SLW_FW_` is a 6-lane freeway with 3.353 m lanes and a 14 m gap between
    inner lane centres, with `LHalf`/`RHalf` pieces for one carriageway.
- **Look:** not available (no local shapes).
- **Profile needs:**
  - 3.65/3.9 m lane families and a median member for FW;
  - the junction member for 46 shapes;
  - `SLW_7thSt_2LS_012m.S` lists its paths right to left.

#### 2.7 Dale Rickert roads (`RR_*`)

- **Source:** `_INFO` "Road shapes by Dale Rickert" and "Road sections by Dale
  Rickert"; the developer list: "Dale Rickert (scale rail and scale road
  pieces)". The lane offsets (±1.95) match ScaleRail's. So these are probably
  ScaleRail-compatible roads (unconfirmed).
  - Dale Rickert (Launchpad user `dmrick44`, as in the developer list's
    address) is the author of The Siskiyou Route (Open Rails bug 1529538,
    2015, <https://bugs.launchpad.net/or/+bug/1529538>): "over 330 miles of
    mainline track" with branches to Diston, White City and Yreka, first
    beta in April 2015
    (<https://web.archive.org/web/20200502130920/http://www.siskurail.org/about.php>).
    The route needs ScaleRail and Scale Roads
    (<https://tsforum.forumotion.net/t902-the-siskiyou-route>); its
    installer writes tsection Build 52 and brings the textures
    (<https://web.archive.org/web/20210619174537/http://siskurail.org/downloadRoute.php>).
    So the `RR_` roads were most likely made for this route (the picture
    search's inference; no page names the `RR_` shapes).
  - The route is withdrawn. On trainsim.com (thread 2306670, 2024-06-26)
    posters say the author pulled it and has not been reachable for over
    three years; a former volunteer has later updates he cannot release.
    The thread gives "setup-20201201.rar" as the last public version; the
    last archived download page (2021-06-19) offers `setup_20210323.rar`,
    which the Wayback Machine holds (not downloaded). Which was the last is
    not settled. If the `RR_` shapes survive, that installer is the
    likeliest place (unconfirmed). The sites siskurail.com and .org are
    gone.
  - Pictures: `/home/arch/tsection-families-web/pictures/road-rickert-rr/`
    (2 route views, low confidence: the route also uses ScaleRoad).
- **Shapes:** 261 / 0 (`RR_Rur_a_` 105, `RR_Int_a_` 39, `RR_Int_c_` 39,
  `RR_Int_a_m_` 38, `RR_Int_c_m_` 38, `RR_City_` 1, `RR_Rural_` 1). None
  local. PRUDNIK: 0.
- **Kind:** road; gauge field 1.5.
- **Contents:**
  - `RR_Rur_a_` is 101 single-path pieces named `1L` (`1LStr`, `1LCrv`;
    one lane, or one path for a narrow road, unconfirmed), plus `Xvr`,
    `Tee`, `Elb`, `Wye`, `Mrg` (4 junctions).
  - `RR_Int_*` is a divided 4-lane road ("Int" = interstate, unconfirmed):
    3.7 m lanes, inner lane centres ±4.21 (`a`, `c`) or ±9.81 (`_m_`).
  - `a`/`c` are probably texture variants (unconfirmed).
  - `RR_Int_*_4LMed_to_4LNoMed` and `RR_City_2L_to_Rural_2L` are lane
    changes.
- **Look:** not available.
- **Profile needs:**
  - a single-path rural road (ROAD MAIN on one path: the whole road);
  - a divided road with a median member;
  - transitions `Med_to_NoMed` and `City_to_Rural` (a loft between two
    profiles, not in the design).

#### 2.8 Bjorn van Steenbergen roads (`DR_Rd_Type`)

- **Source:** `_INFO` "Track/Road Sections from Bjorn van Steenbergen"; set
  identity: see the rail section (`DR_*A` track). Dutch names: Afslag =
  exit, Oversteek = crossing, Voorsorteer = turn lanes ahead of a junction,
  Tankstation = filling station.
- **Shapes:** 94 / 94. None local. PRUDNIK: 0.
- **Kind:** road pieces of a track-and-road set; gauge field 1.5.
- **Contents:**
  - road types 1-4. Type 1/2: 2 paths ±1.5. Type 3/4: 4 paths at -3, 3,
    -6, 6, listed out of order, so 3 m lanes with a 3 m gap at the centre;
  - 11 junctions (`_T`, `_Cross`, `Type3to2_T`, `Type1to2_Cross`);
  - 6 lane changes and 4 plain `Afslag` pieces. Their extra paths at
    -13.8..-24.7 m (and +) run parallel: a separate exit or service road
    beside the main road;
  - `Tankstation`, `Voorsorteer`, `Oversteek`.
- **Look:** not available.
- **Profile needs:**
  - roles that sort paths by offset (31 shapes listed out of order, 18 of
    them plain);
  - a centre gap part (type 3/4);
  - the junction member for 17;
  - a parallel second road inside one shape (Afslag) = two groups of
    parallel paths with a gap: the stretched member must split at gaps
    larger than a median.

#### 2.9 R. Fiore Italian roads (`xroad`, `xhwy`, `xhwyt`)

- **Source:** `_INFO` "Additional Track and Road Sections by R.Fiore |
  xHighways" and "xroads"; the developer list: "Raffaele Fiore Track & Road
  Sections". Italian words in the names (ponte = bridge, sx/dx = left/right).
  The country is from names; the package is unconfirmed. Raffaele Fiore also
  made Italian rolling-stock models (SimTreni, 2010-2011,
  <https://www.simtreni.com/download/index.php?subcat=14&sort=titleD&start=0>);
  no road package was found in either round.
- **Shapes:** 39 / 0 (`xroad` 23, `xhwy` 13, `xhwyt` 3). None local.
  PRUDNIK: 0.
- **Kind:** road; gauge field 1.5.
- **Contents:**
  - `xroad2l`: 3.5 m lanes, straights, curves and 63/70 m bridges
    (`_ponte`).
  - `xroad4l`: 3 m lanes, inner centres ±4.
  - `xhwy`: single (`sngl`, ±1.86) and double (`dbl`, ±3.54/±7.26)
    carriageways, bridges `brdg`, `brdg_sx`/`dx`, tunnels `tunsx`/`tundx`,
    500 m curves as inner/outer carriageway (`sngl_int`/`ext`).
  - No junctions.
- **Look:** not available.
- **Profile needs:**
  - families with 3.5 and 3.72 m lanes, a median member;
  - road bridge and road tunnel profiles: a deck with parapets; a tube
    polyline for a road tunnel. Neither exists for roads yet; the design's
    tunnel and bridge families are track.

#### 2.10 Kraków-Kocmyrzów avenue (`KK_Alpl`)

- **Source:** PSK only. `_INFO` "Krakow_Kocmyrzow Aleja pl 6 letniego od
  Tabore". By the words: an avenue ("Aleja Planu 6-letniego", Nowa Huta) for
  the Kraków-Kocmyrzów route, "from Tabore".
  - Tabore is the author of that route, "Linia 111 Kraków - Kocmyrzów
    (Kocmyrzówka)" (period 1961-1963, route folder `KK_1961_1963`): "the
    originator, author of the route, and of many objects is Tabore"
    (translated from Polish; <https://dodatki.kolej.org.pl/111_autorzy.htm>).
    He is known in MSTS for the SN61 railcar and the "Ryflaki" and "Bipy"
    coaches. He did not finish the route; his last version is from 2012
    (<https://pkor.trainsim.pl/trasa-krakow-kocmyrzow/>, downloadable,
    180 MB), and in 2021 Emil S. and others revived it with timetables. So
    `KK_` = Kraków-Kocmyrzów, from this route.

  Polish piece names:
  - `pasy`: zebra crossing;
  - `przystT` / `przystanek`: tram stop;
  - `przejscie`: pedestrian crossing;
  - `przecznica`: side street;
  - `agrafka`: literally "safety pin", meaning here unconfirmed.
- **Shapes:** 0 / 23, all local in OR_PSK_NEW. PRUDNIK: 76 objects (13
  shapes).
- **Kind:** road with a tram track in the middle (no train runs on a road
  shape). The gauge field is 1.5, and 0.5 on its own curve sections.
- **Contents:**
  - 6-path pieces: car lanes ±6.5/±9.5, and paths ±1.85, probably the tram
    tracks (tram on road paths, perhaps for car-spawner trams;
    unconfirmed);
  - 2-path pieces carry only the ±1.85 pair;
  - straights 2-200 m, curves r 190 and 290;
  - the 290 m curves use two radii for six paths (definition error);
  - no geometric junctions, but `przecznica` has a side street drawn into
    the mesh (to x = -43 m).
- **Look** (looked at `KK_Alpl6l_15mStrt`, `KK_Alpl6l_50mStrt`,
  `KK_Alpl6l_przystT`, `KK_Alpl6_przecznica`, `KK_Alpl6_pasy_8mStrt`,
  `KK_Alpl6_agrafka_8mStrt`):
  - 42.5 m wide: two carriageways (`KK_Jezdnia_1`) on raised kerbs at
    0.2-0.3 m;
  - a ballasted tram track with rails in the middle (`KK_Tor_tram_*`, up to
    0.33 m);
  - verges sloping to -0.92 m (`KK_Jezdnia_2`);
  - the tram stop piece has a platform, fence and shelter (up to 2.95 m).
- **Profile needs:**
  - a ROAD family with an embedded tram reservation: ballast, sleepers and
    rails as polylines or Template3D in a ROAD member;
  - kerbs and verges;
  - the stop, crossing and side-street pieces as static shapes or Place
    items;
  - keep `DISABLED` until such a family exists;
  - sweep along one reference path because of the definition errors.

#### 2.11 Magnetic platforms by Graham Pitt (`Plat`, `WhitePlat`, `MarblePlat`, `AshphaltPlat`)

- **Source:** `_INFO` "Magetic Platform Track Shapes by Graham Pitt" and
  "Magnetic Platform Sections by Graham Pitt"; the developer list: "Graham
  Pitt (Platforms)". Four kits, uploaded to UKTrainSim on 2002-10-09 by
  Graham Pitt and now on trainsim.com under the same ids: Kit I fid 1186
  ("Platform Kit I Normal Texture Version 1.03"; the `plat*` shapes and
  `platcurve.ace`, the `Plat` prefix), Kit II White Stone fid 1187, Kit III
  Marble fid 1210, Kit IV Ashphalt fid 1211.
  - The readme (fid 1186): "Each kit features pieces that snap together
    like magnets. This 'Magnetic' feature works in similar manner to the
    way that tracks automatically align themselves". So "magnetic" means the
    pieces connect through `tsection.dat` like track.
  - Installation: the `.s`/`.sd` files go to `Global\Shapes`, the `.ace` to
    each route's `Textures`. An alternative installation without the
    magnetic feature uses route shapes and a `.ref` entry.
  - Use: wide, UK first. Routes name the kits as required: YCR 1.51 (fid
    11708, "kits I, II and IV"), West Anglia Main Line (fid 20950),
    London-Southampton (fid 13446), South Devon Main Line (fid 19558),
    Manchester Metrolink (fid 17461), BrisCard V6 (fid 30305), Dabley (fid
    55920: "If you don't have Graham Pitt's magnetic platforms installed the
    route won't load"), Peak Rail (fid 5511), La Mure (fid 60984). A
    zip-content search for `Plat20m` finds 58 uploads that ship the shapes,
    among them Mumbai-Amritsar (fid 78819), High Speed 1, NWE v3.0, New
    Forest V2, Chiltern, NSW Joppa Junction (2025) and Vijayawada routes
    (2026).
  - Pictures: `/home/arch/tsection-families-web/pictures/platforms-pitt/`
    (2, trainsim.com's untextured renders of `plat20m250r` and
    `plat10mend`).
- **Shapes:** 8 per prefix, 32 / 32. None local. PRUDNIK: 0.
- **Kind:** platforms flagged as road shapes. As road shapes they join end to
  end in the editor: the readme's "magnetic".
- **Contents:** one path; 10 m and 20 m straights, 20 m pieces on radii
  250, 500, 1000, 1500 and 2000 m (to follow curved track), and a 10 m end
  piece. "Each platform section is 7m ... wide, 1.37m ... high" (readme).
- **Look:** not available. The surfaces are plain ("Normal Texture"),
  white stone, marble and asphalt (sic "Ashphalt").
- **Profile needs:** ROAD MAIN with a platform cross-section (edge face,
  coping, surface) 7 m wide and 1.37 m high, one family per surface. Task
  17 already lists these under "9 road (platforms)" for the A namespace
  (`AshphaltPlat`).

### 3. Small sets

| Prefix | Shapes (B54 / PSK) | Local | PRUDNIK | What it is | Belongs to |
| --- | --- | --- | ---: | --- | --- |
| `prrw` | 4 / 4 | 4 | 0 | 48 m runway: 80 and 160 m straights (Bruce Bridges, Conrail Indianapolis route), end and 60° cross (Masters) | 2.2 |
| `rd` | 12 / 12 | 12 | 0 | Martyn T. Griffin's residential city roads: 14.28 m with kerbs, sidewalks, transparent lawn; straight, driveway, 22.5° and 90° bends, intersection, T, court dead end | 2.2 |
| `Plat`, `WhitePlat`, `MarblePlat`, `AshphaltPlat` | 8 / 8 each | 0 | 0 | Graham Pitt's platform pieces | 2.11 |
| `xhwyt` | 3 / 0 | 0 | 0 | double-carriageway 500 m curves, 2/10/20° | 2.9 |
| `SLW` | 2 / 0 | 0 | 0 | `SLW_NC-U_2L-2LTee_90d` (T junction), `SLW_UR_Parker_X` (crossing) | 2.6 |
| `SLW_` | 5 / 0 | 0 | 0 | "7th Street" straights and a curve | 2.6 |
| `SLW_UX_`, `SLW_NX_` | 4 / 0 each | 0 | 0 | 2-lane straights 15-30 m (X = crossing? unconfirmed) | 2.6 |
| `SLW_UR_SB_` | 1 / 0 | 0 | 0 | one 2-lane 10 m straight | 2.6 |
| `RR_City_` | 1 / 0 | 0 | 0 | `RR_City_2L_to_Rural_2L`, a transition | 2.7 |
| `RR_Rural_` | 1 / 0 | 0 | 0 | one 2-lane 1500 m curve | 2.7 |

Not in `tsection.dat` but used on PRUDNIK on road geometry:

- PSK's `GLOBAL/SHAPES/DS/` has 382 reskins of Kuju road shapes
  (`DS1_`, `DS1b_`, `DS1s_`, `DS1bpl_`/`bpr_`/`bp2_`, ...). They use
  `BSTtex01/02.ace`: asphalt with edge lines, a paved strip, gravel, grass,
  and guard rails left, right or both. 14 PRUDNIK objects. They are Norbert
  Rieger's DBTracks "DS1 Roads": "DS1_Roads.rar, 10.6 MB, 342 pieces,
  27.04.2012: DS1 Roads package, replacement for MSTS standard roads", with
  delineators and traffic barriers "according to German standards"
  (<https://dbtracks.com/pages/DBTracks10.html>). Why PSK has 382 against
  the package's 342 is not explained, and neither are the letters.
- The five `BAB_*` folders are Rieger's DBTracks variants, described in
  2.4.

### 4. Open points

1. **Road side roles.** Today's `LEFT/MIDDLE/RIGHT` rule (listing order,
   start heading, fixed lane width) does not fit:
   - medians and pathless centre lanes: 764 shapes;
   - lane widths other than the profile's: 3.0-3.9 m;
   - paths listed out of order: 82 shapes;
   - lane add/drop pieces.

   Proposal: a ROAD member stretched between the outermost paths of each
   group, with lane lines between adjacent paths and a median part at large
   gaps. This is the track `COMPLEX` bed rule applied to roads. Not in the
   design; it needs a decision.
2. **Road junctions.** 296 junctions and 59 lane changes. The track
   `COMPLEX` member does not cover them (no rails or ties; it needs a filled
   surface, kerb returns, dropped markings, stop lines, gores). Until a road
   junction member exists, `Filter ( Kind ( Complex ) ... Profile ( DISABLED ) )`
   must also catch road junctions. The design's `Complex` rule is phrased
   with a tie length, which ROAD families do not have.
3. **Direction.** One-way, two-way and left-hand (UK) running are not in the
   geometry. Profiles must be chosen by name (`1w`/`1W`, `2w`, `LT`, `uk`,
   `l`/`r`). Mirrored placement swaps asymmetric markings: does TSRE know a
   road object's placed direction, to mirror a one-way member?
4. **`Forced` and roads.** The policy looks up `DefaultTrack` for roads too;
   `RdProfile` (the documented default road family) is not used. Without
   the filter file, `Forced` leaves every road static unless the route has a
   ROAD profile called `DefaultTrack`. Intended?
5. **Definition errors.** 16 curves (8 Kuju `US1Rd4l`, 4 ScaleRail, 4
   KK_Alpl) have lane paths that do not stay parallel. Sweep along one
   reference path, or keep them static.
6. **Look-only variants.** 164 intersection-kit pieces, 112 marking pieces
   and 8 road-type transitions have plain geometry but special textures. They
   need texture variants (one family per marking), decals (`Place`), a loft
   between two profiles (transitions, not in the design), or stay static.
7. **Road bridges and tunnels.** `xhwy` bridges and tunnels, `xroad` bridges,
   PSK's `BAB_2w_br`/`Br_BAB_2w`, `BAB_2w_emb` embankments up to 16 m: the
   design's tunnel and bridge families are track only.
8. **Embedded tram track** (KK_Alpl) and **platforms** flagged as roads: ROAD
   members that hold rails or a platform section. Possible with polylines and
   Template3D, but no family exists.
9. **Unidentified.** The second round (2026-11-26/27) answered:
   - Dale Rickert's package: none found; the roads were most likely made
     for his Siskiyou Route (2.7);
   - the authors of PSK's `DS/` and `BAB_*` folders: Norbert Rieger,
     DBTracks (2.4 and section 3);
   - the `BAB_*` folder letters `1w`/`2w`, `1g`/`2g`, `B`, `br`, `emb`,
     `ramp` (2.4);
   - "Tabore": the author of the Kraków-Kocmyrzów route (2.10);
   - why the platforms are "magnetic" (2.11).

   Still unknown:
   - the J.A. Norton set's name and `SLW` / `UR` / `NC` / `SB` / `UX` / `NX`;
   - Rickert's `a` / `c` / `m` letters;
   - R. Fiore's package;
   - the `BAB` letters `GR`, `EM`, `E`, `M`, `oS` (DBTracks' `BAB_Facts.rar`,
     "Instructions and description of the entrances and exits", probably
     explains some; not opened);
   - the `Fwy` (`LT`, `LW`) origin;
   - Kraus-Yao's `sp`;
   - the `DS/` letters.

   For these, everything said comes from tsection credits, names and
   meshes.
10. **Textures for ScaleRail roads** (`SR_Road_*.ace`) and all shapes of
    sets 2.6-2.9 and 2.11 are not on this machine. Their look is from
    meshes, names and, where found, pictures. What exists (nothing
    downloaded):
    - the ScaleRail road textures come with routes and are in "Saluda Road
      Textures" (fid 69357; 45 `SR_Road_*.ace` and 22 snow ones); the
      ScaleRoad installer is fid 87684 and Elvas Tower file 3208 (2.5);
    - Graham Pitt's platform kits I-IV: fids 1186, 1187, 1210, 1211 (2.11);
    - the `RR_` shapes, probably, in The Siskiyou Route's last installer,
      which the Wayback Machine holds (2.7);
    - nothing for `SLW_`, `DR_Rd_Type` and Fiore's roads.

    Pictures in `/home/arch/tsection-families-web/pictures/`:
    `road-scalerail` (2), `road-rickert-rr` (2, low confidence),
    `road-bab` (1), `platforms-pitt` (2 untextured renders); none for
    `SLW_`, Fiore and `DR_Rd_Type`.
