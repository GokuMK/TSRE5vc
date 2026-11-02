# Wyświetlacze: tablice odjazdów, ekrany peronowe i wyświetlacze w pociągach

*English version: [Displays](displays.md).*

**Wyświetlacz** pokazuje na kształcie żywy dokument: tablicę odjazdów na
stacji, ekran peronowy z najbliższym pociągiem i zegarem, wyświetlacz
kierunkowy pociągu, panel LED przy drzwiach wagonu. TSRE rysuje dokument do
tekstury, a kształt ją pokazuje, w dzień i w nocy: w widoku 3D edytora tras,
podczas symulacji i w przeglądarce modeli 3D (Shape Viewer).

Dokument to plik [RmlUi](https://mikke89.github.io/RmlUiDoc/) (`.rml`):
znaczniki podobne do HTML ze stylami podobnymi do CSS. TSRE wypełnia go
danymi (zegar, odjazdy i przyjazdy stacji, dane pociągu) i rysuje go ponownie
tak często, jak chce tego wyświetlacz.

Przykłady pochodzą z trasy Prudnik w OR_PSK; warto otworzyć ich dokumenty
obok tego poradnika.

## 1. Dwa rodzaje wyświetlaczy

| Rodzaj | Co to jest | Kiedy go użyć |
| --- | --- | --- |
| **Podmiana tekstury** | Jeden z obrazów tekstur kształtu (wskazany nazwą pliku) zostaje w całości zastąpiony dokumentem. Każda część kształtu, która używa tego obrazu, pokazuje dokument. | Wyświetlacz ma własną teksturę: lico tablicy, tekstura wyświetlacza kierunkowego pociągu. |
| **Prostokąt** | Płaski prostokąt, który TSRE dodaje do kształtu, zawieszony na jednej z jego macierzy, z położeniem, obrotem i rozmiarem. Porusza się razem z tą macierzą (drzwi, pantografy). | Wyświetlacz jest namalowany na wspólnej teksturze (atlasie) albo ma się znaleźć tam, gdzie kształt go nie ma. |

Podmiana tekstury rysuje dokument na **całej teksturze**: rozmieść treść
dokumentu tak, jak kształt mapuje teksturę. Fragmenty tekstury (prostokąty
UV) nie są obsługiwane.

![Tablica odjazdów: jej tekstura zastąpiona dokumentem](images/displays-board.jpg)

## 2. Wyświetlacz na kształcie stacyjnym

Tablica odjazdów `ORB_Tpo4` ma na swoje lico osobną teksturę,
`ORB_Tpo_ODJAZDY.ACE`.

1. Umieść dokument w folderze `DISPLAYS` trasy:
   `ROUTES/PRUDNIK/DISPLAYS/odjazdy.rml`.
2. Dodaj blok do pliku `.sd` kształtu (`ROUTES/PRUDNIK/SHAPES/ORB_Tpo4.sd`),
   wewnątrz jego bloku `shape ( … )`:

   ```
   ESD_TSREDisplay (
       Document ( "odjazdy.rml" )
       Image ( "ORB_Tpo_ODJAZDY.ACE" )
       Resolution ( 512 1024 )
       Surface ( Paper )
       RefreshRate ( 0.2 )
       Input ( Station )
   )
   ```
3. Otwórz trasę. Tam, gdzie stoi tablica, TSRE znajduje najbliższy peron i
   jego stację, a tablica pokazuje pociągi tej stacji. Uruchom symulację
   (albo rozkład jazdy Open Rails), żeby ją wypełnić.

Ekran na wiszącym zegarze, którego wyświetlacz LCD jest namalowany na
wspólnej teksturze, dostaje zamiast tego prostokąt:

```
ESD_TSREDisplay (
    Document ( "peron_lcd.rml" )
    ShapeHierarchy ( "MAIN" )
    Position ( -0.207 0.414 0.001 )
    Rotation ( 8.4 -90 0 )
    Size ( 1.389 0.670 0 )
    Resolution ( 1040 500 )
    Surface ( Glass )
    RefreshRate ( 1 )
    Input ( Platform )
)
```

![Ekran peronowy: prostokąt na wyświetlaczu LCD zegara](images/displays-platform.jpg)

## 3. Wyświetlacz w pociągu

Wyświetlacze wagonu definiuje się tak samo, w pliku `.sd` jego kształtu, a
dokumenty leżą w folderze `DISPLAYS` zestawu taboru
(`TRAINS/TRAINSET/<folder>/DISPLAYS`). Z `Input ( Train )` dokument dostaje
pociąg, w którym jedzie wagon: symulacja przekazuje każdemu wagonowi jego
pociąg.

Wagon Z2B (`PKP_Z2B-OR/PKPIC_B11mnouz-70082.sd`) ma cztery prostokąty: dwa
na pudle, na namalowanych panelach LED, i dwa na skrzydłach drzwi, które
przesuwają się razem z drzwiami:

```
ESD_TSREDisplay (
    Document ( "z2_drzwi.rml" )
    ShapeHierarchy ( "DOOR_B" )
    Position ( 0.076 -0.625 0 )
    Rotation ( 0 90 0 )
    Size ( 0.5 0.135 0 )
    Resolution ( 624 168 )
    Surface ( Glass )
    RefreshRate ( 0.2 )
    Input ( Train )
)
```

`Position` podaje się w układzie współrzędnych macierzy: tutaj 7,6 cm na
zewnątrz od początku układu skrzydła drzwi i 62,5 cm poniżej niego.

![Panele wagonu: na pudle i na otwartym skrzydle drzwi](images/displays-coach.jpg)

EN75 (`KS_EN75`) ma teksturę, na której są tylko jego wyświetlacze
kierunkowe, `EN75_wyswietlacz.ace`: to podmiana tekstury, a dokument rysuje
pasy czołowe i boczne tam, gdzie mapuje je kształt.

![Wyświetlacz kierunkowy EZT (przykładowy pociąg przeglądarki modeli 3D)](images/displays-emu.jpg)

## 4. Blok w pliku `.sd`

Jeden blok `ESD_TSREDisplay ( … )` na wyświetlacz, wewnątrz bloku
`shape ( … )` pliku `.sd`; każda wartość to nazwany wpis. Wielkość liter w
nazwach nie ma znaczenia. Plik `.sd` musi być plikiem tekstowym (Unicode).

| Wpis | Wartości | Znaczenie |
| --- | --- | --- |
| `Document` | `"plik.rml"` | Dokument, w folderze `DISPLAYS` trasy albo zestawu taboru; może leżeć w podfolderze (`"tablice/odjazdy.rml"`). Wymagany. |
| `Image` | `"nazwa.ace"` | Podmiana tekstury: obraz tekstury, tak jak nazywa go lista `images` kształtu (bez folderu). |
| `ShapeHierarchy` | `"MACIERZ"` | Prostokąt: macierz, na której wisi. Opcjonalny: domyślnie korzeń kształtu. |
| `Position` | `x y z` | Prostokąt: jego środek, w metrach, w układzie tej macierzy. |
| `Rotation` | `x y z` | Prostokąt: stopnie wokół x, potem y, potem z. Bez obrotu prostokąt leży w płaszczyźnie x–y, zwrócony w stronę +z. Opcjonalny. |
| `Size` | `szer wys 0` | Prostokąt: szerokość i wysokość w metrach (trzecia wartość to zawsze 0). |
| `Resolution` | `szer wys` | Rozmiar dokumentu w pikselach. Opcjonalny dla `Image` (rozmiar tekstury); dla prostokąta domyślnie 512 × 256. Zachowaj proporcje prostokąta. |
| `Surface` | `Glass` albo `Paper` | `Glass` świeci tak, jak go narysowano, o każdej porze (ekran, panel LED); `Paper` oświetla scena, w nocy jest ciemny, chyba że dosięga go lampa. Opcjonalny: podmieniona tekstura zachowuje materiał swoich części, prostokąt jest `Glass`. |
| `RefreshRate` | `n` | Odświeżenia na sekundę; `0` rysuje dokument raz. |
| `Input` | `Platform`, `Station`, `Train` | Co dokument dostaje oprócz zegara (rozdział 6). Opcjonalny: wtedy tylko zegar. `Clock` znaczy to samo. |

Blok potrzebuje wpisu `Document` oraz albo `Image`, albo `Position` i
`Size`; w przeciwnym razie jest pomijany (i zgłaszany, rozdział 9).

## 5. Pisanie dokumentów

- **Model danych:** `body` dokumentu wiąże model danych `display`:
  `<body data-model="display">`. Teksty to `{{station}}`, listy
  `data-for="row, i : departures"`, warunki `data-if="…"`, a powiązać można
  każdy atrybut i styl (`data-attr-src`, `data-style-…`). Szczegóły w
  dokumentacji wiązania danych (data bindings) RmlUi.
- **Czcionki:** czcionki interfejsu TSRE, `Noto Sans` zwykła i pogrubiona.
- **Obrazy:** `<img src="…">` i `decorator: image(…)` są czytane z
  **folderu tekstur** zasobu, tak jak tekstury jego kształtów: z `TEXTURES`
  trasy dla dokumentu trasy, z własnego folderu zestawu taboru dla dokumentu
  pociągu. Formaty: ACE, DDS, PNG, JPG, BMP, TGA. Obrazy wczytują się w tle;
  wyświetlacz narysowany, zanim dotrą, jest rysowany ponownie, gdy tylko
  będą gotowe.
- **Arkusze stylów i skrypty** (`.rcss`, `.lua`) są czytane z folderu
  dokumentu.
- **Współdzielenie:** wyświetlacze, które pokazują ten sam dokument z tymi
  samymi danymi wejściowymi (ten sam peron, stacja albo pociąg) w tej samej
  rozdzielczości, dzielą jedną teksturę: tablice jednej stacji, wagony
  jednego pociągu.
- **Odświeżanie:** wyświetlacz jest rysowany ponownie tak, jak podaje
  `RefreshRate`, gdy jest w polu widzenia. W jednej klatce rysuje się
  najwyżej tyle dokumentów, ile podaje ustawienie
  `Odświeżenia wyświetlaczy na klatkę` (`core.rendering.displayRedraws`,
  Ustawienia, domyślnie 2), w tym najwyżej jeden wczytywany: najpierw te,
  które czekają najdłużej, bliższe przed dalszymi.

### Lua

Do większości dokumentów wystarczy wiązanie danych. Dokument może też
uruchamiać Lua (wtyczka Lua do RmlUi): `<script src="plik.lua"></script>` w
nagłówku i procedury obsługi zdarzeń w elementach. Przy każdym odświeżeniu
TSRE wysyła do dokumentu zdarzenie `displayupdate`, a
`event.parameters.time` to liczba sekund zegara od północy:

```
<body data-model="display" ondisplayupdate="z2DrzwiUpdate(event, document)">
```

```
function z2DrzwiUpdate(event, document)
    local english = math.floor(event.parameters.time / 4) % 2 == 1
    document:GetElementById("pl"):SetClass("hidden", english)
    document:GetElementById("en"):SetClass("hidden", not english)
end
```

Wszystkie dokumenty dzielą jeden stan Lua: nadawaj funkcjom globalnym takie
nazwy, które nie zderzą się z funkcjami innego dokumentu. Lua działa z
limitem czasu.

## 6. Co dokument może odczytać

Każdy dokument:

| Nazwa | Wartość |
| --- | --- |
| `time` | Zegar, `HH:mm` (symulacji, gdy działa, w przeciwnym razie trasy). |
| `date` | `yyyy-MM-dd`. |
| `hours`, `minutes`, `seconds` | Liczby, do wskazówek zegara (`data-style-transform`). |

Z `Input ( Platform )` (najbliższa krawędź peronu w promieniu 300 m; dla
prostokąta liczona od miejsca 3 m przed nim, gdzie stoi patrzący) albo
`Input ( Station )` (wszystkie perony stacji):

| Nazwa | Wartość |
| --- | --- |
| `station`, `platform` | Nazwa stacji; nazwa peronu (nie dla `Station`). |
| `departures`, `arrivals` | Listy wierszy, według czasu; postój, który już minął, jest pomijany. |
| `row.time` | Czas według rozkładu. |
| `row.train` | Nazwa pociągu (jego nazwa w rozkładzie). |
| `row.origin`, `row.destination` | Stacje jego pierwszego i ostatniego postoju na trasie (puste, gdy pociąg w tym miejscu wjeżdża na trasę albo z niej zjeżdża). |
| `row.platform` | Peron, przy którym się zatrzymuje. |
| `row.delay` | Minuty opóźnienia; puste, gdy pociąg jedzie punktualnie. |
| `row.extra.<klucz>` | Własne dane pociągu (rozdział 7); pusty tekst dla klucza, którego pociąg nie ma. |

Z `Input ( Train )` (wyświetlacz wagonu):

| Nazwa | Wartość |
| --- | --- |
| `train.name` | Jego nazwa w rozkładzie. |
| `train.origin`, `train.destination` | Stacje jego pierwszego i ostatniego postoju. |
| `train.state` | `waiting` (jeszcze nie wyruszył), `standing` (stoi na stacji), `held` (zatrzymany gdzie indziej), `running` (w drodze), `approaching` (do najbliższego postoju zostało najwyżej 500 m), `finished` (zakończył bieg); puste bez symulacji. |
| `train.at` | Stacja, na której stoi. |
| `train.next` | Następny postój, do którego dojedzie (gdy stoi: kolejny po tym). |
| `train.previous` | Ostatnia stacja, z której odjechał. |
| `train.platform` | Peron, przy którym stoi, w przeciwnym razie peron następnego postoju. |
| `train.departs`, `train.arrives` | Odjazd według rozkładu z miejsca, w którym stoi (przed wyruszeniem: z pierwszego postoju); przyjazd według rozkładu do `next`. |
| `train.distance` | Metry do następnego postoju, jeśli są znane. |
| `train.delay` | Minuty opóźnienia. |
| `train.extra.<klucz>` | Jego własne dane (rozdział 7). |

Gdy pociąg nie ma własnych danych albo jedzie dalej niż trasa, pokaż dane,
jeśli są, a jeśli nie, to wartość zastępczą:

```
{{ row.extra.destination != '' ? row.extra.destination : row.destination }}
```

## 7. Dane pociągów w rozkładzie jazdy

Rozkład jazdy Open Rails nazywa każdy pociąg raz, nagłówkiem jego kolumny;
nie ma pola na numer, kategorię ani przewoźnika, więc autorzy często
upychają je w nazwie (`0846_IR46140SZYNDZIELNIA_KK-O`). Żeby pokazać je
osobno, zapisz je w osobnych wierszach:

```
#tsre.extra.category;;IR;TLK;…
#tsre.extra.number;;61120;62102;…
#tsre.extra.name;;PIAST;SZTYGAR;…
```

- Jeden wiersz na klucz: `#tsre.extra.` i klucz; komórka pociągu to jego
  wartość. Klucz może być dowolny: dokumenty czytają te, których potrzebują
  (`row.extra.number`, `train.extra.number`).
- Klucze pisz małymi literami (Open Rails zamienia pierwszą komórkę wiersza
  na małe litery), tylko z liter a–z, cyfr i `_`.
- Pusta komórka: pociąg nie ma wartości dla tego klucza.
- Open Rails czyta taki rozkład: każdy z tych wierszy daje w jego dzienniku
  jedną linię „Invalid row definition”, a sam wiersz jest pomijany. Używaj
  wierszy, nigdy kolumn.
- Okno rozkładu jazdy TSRE zapisuje te wiersze z powrotem przy zapisie.

Przykłady używają kluczy `operator`, `category`, `number`, `line`, `name`,
`origin` i `destination` (skąd pociąg przyjeżdża albo dokąd jedzie poza
trasą) oraz `kind` (`fast`, `regional`, `aglo`) dla piktogramów rodzaju
pociągu na tablicach (`rodzaj_pociagu_<kind>.png` w `TEXTURES` trasy).

## 8. Szkło, papier i noc

- `Glass`: kolory dokumentu takie, jak je narysowano, o każdej porze, jak
  podświetlony ekran. Prostokąt jest szkłem, chyba że blok mówi inaczej.
- `Paper`: oświetlony tak, jak oświetla go scena: jasny w dzień, w nocy
  ciemny, chyba że dosięga go lampa. Drukowana tablica.
- Bez `Surface` części podmienionej tekstury zachowują własny materiał
  kształtu, łącznie z PBR.

## 9. Komunikaty i rozwiązywanie problemów

Problemy pojawiają się w **Okno > Błędy i komunikaty**, każdy jeden raz:

- pominięty blok (brak `Document`; ani `Image`, ani `Position` i `Size`);
- nie znaleziono dokumentu albo leży on poza folderem `DISPLAYS`;
- `Image`, którego kształt nie ma (nic nie pokazuje dokumentu);
- `ShapeHierarchy`, której kształt nie ma (prostokąt wisi na korzeniu
  kształtu);
- nie znaleziono obrazu;
- dokument się nie wczytał, a także błędy i ostrzeżenia RmlUi w dokumencie
  (szczegóły są w dzienniku).

W przeglądarce modeli 3D wyświetlacze kształtu pokazują przykładowe dane:
stację z odjazdami i przyjazdami oraz pociąg (IC 108 ADRIATIC).

## 10. Przykłady w OR_PSK

| Dokument | Kształt | Rodzaj | Input |
| --- | --- | --- | --- |
| `ROUTES/PRUDNIK/DISPLAYS/odjazdy.rml`, `przyjazdy.rml` | `ORB_Tpo4` (tablice odjazdów i przyjazdów) | Tekstura, papier | Station |
| `ROUTES/PRUDNIK/DISPLAYS/peron_lcd.rml` | `wyswietlacz_zegar_wiszacy_1`, `wyswietlacz_peron_zegar_wiszacy` | Prostokąty, szkło | Platform |
| `TRAINS/TRAINSET/KS_EN75/DISPLAYS/en75_wyswietlacze.rml` | `KS_EN75_A`, `KS_EN75_B` | Tekstura, szkło | Train |
| `TRAINS/TRAINSET/PKP_Z2B-OR/DISPLAYS/z2_drzwi.rml` | `PKPIC_B11mnouz-70082` (pudło i skrzydła drzwi) | Prostokąty, szkło | Train |
| `TRAINS/TRAINSET/PKP_Z2B-OR/DISPLAYS/z2_drzwi_lua.rml` (+ `z2_drzwi.lua`) | `PKPIC_B11mnouz-70085` | Prostokąty, szkło, Lua | Train |

Każdy dokument zaczyna się od komentarza (po angielsku) o tym, co pokazuje
i jak. Rozkład `PSK_2026` ma wiersze `#tsre.extra` dla pociągów, które
pokazują przykłady.

## 11. Znane ograniczenia

- Wyświetlacze na obiektach statycznych i na wagonach; jeszcze nie na
  sygnalizatorach, przejazdach kolejowych ani innych obiektach z kształtami.
- Na razie tylko kształty MSTS (kształty glTF później); tylko tekstowy plik
  `.sd`.
- Odświeżanie wyświetlacza liczy czas rzeczywisty: podczas pauzy
  wyświetlacz zegara wciąż jest rysowany ponownie (ten sam obraz).
- Wyświetlacze w kabinie (ekrany CVF) i klikanie w wyświetlacz nie wchodzą
  w zakres tej funkcji.
