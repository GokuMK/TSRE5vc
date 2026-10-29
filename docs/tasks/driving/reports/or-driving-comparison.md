# Driving against Open Rails (task driving 02, step 5)

Run 2026-10-28: the same commands in both, TSRE's `simulation-route` suite with `TSRE_SIM_DRIVE` and `scripts/orts-drive-run.sh` (Open Rails' web API, real time, its driving display read every half second), set side by side by `scripts/drive-compare.py`. Open Rails 8aafb3147 (the Linux fork), its default settings (advanced adhesion). Pressures in psi as both show them; Open Rails' are rounded to whole psi on its display. Open Rails' distance is its speed integrated.

## Full throttle, then full service (the HHP-8, USA1 morningmaryland)

Commands:

```
# USA1 morningmaryland, the player's HHP-8: brakes released, full throttle 90 s, then full service.
0 direction 1
0 trainBrake 0
40 throttle 1
130 throttle 0
131 trainBrake 0.85
260 trainBrake 0.85
```

Open Rails' loco slips the whole time (its display: wheel slip), its force about a third of TSRE's. (TSRE's log had no last car yet in this run.)

| s | TSRE m/s | OR m/s | TSRE m | OR m | TSRE pipe psi | OR pipe psi | TSRE cyl psi | OR cyl psi | TSRE last pipe | OR last pipe | TSRE last cyl | OR last cyl |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 30 | 0.0 | 0.0 | 0 | 0 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 40 | 0.0 | 0.0 | 0 | 0 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 50 | 8.9 | 2.8 | 45 | 14 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 60 | 17.7 | 5.6 | 178 | 56 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 70 | 25.4 | 8.5 | 395 | 127 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 80 | 31.1 | 11.2 | 679 | 225 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 90 | 35.7 | 13.9 | 1013 | 350 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 100 | 39.7 | 16.6 | 1391 | 502 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 110 | 43.1 | 19.2 | 1805 | 681 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 120 | 46.2 | 21.9 | 2252 | 887 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 130 | 48.9 | 24.4 | 2728 | 1119 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 140 | 40.1 | 17.8 | 3185 | 1338 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |
| 150 | 26.8 | 11.5 | 3521 | 1485 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |
| 160 | 10.1 | 4.2 | 3711 | 1566 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |
| 170 | 0.0 | 0.0 | 3736 | 1576 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |
| 180 | 0.0 | 0.0 | 3736 | 1576 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |

- reached 10 m/s: TSRE 51.5 s, Open Rails 76.0 s
- reached 20 m/s: TSRE 63.0 s, Open Rails 113.1 s
- reached 30 m/s: TSRE 78.0 s, Open Rails never
- reached 40 m/s: TSRE 101.0 s, Open Rails never
- top speed: TSRE 48.92 m/s, Open Rails 24.63 m/s
- stopped: TSRE at 165.0 s after 1008 m braking, Open Rails at 164.8 s after 436 m braking

## Throttle 0.6, then full service

Commands:

```
# USA1 morningmaryland, the player's HHP-8: brakes released, throttle 0.6 for 90 s, then full service.
0 direction 1
0 trainBrake 0
40 throttle 0.6
130 throttle 0
131 trainBrake 0.85
260 trainBrake 0.85
```

No slip: the acceleration within 2.5 %. Full service: Open Rails' cars slide (its display: skid), its stop half as hard.

| s | TSRE m/s | OR m/s | TSRE m | OR m | TSRE pipe psi | OR pipe psi | TSRE cyl psi | OR cyl psi | TSRE last pipe | OR last pipe | TSRE last cyl | OR last cyl |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 30 | 0.0 | 0.0 | 0 | 0 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 40 | 0.0 | 0.0 | 0 | 0 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 50 | 5.3 | 5.0 | 27 | 24 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 60 | 10.5 | 10.1 | 105 | 100 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 70 | 15.1 | 14.7 | 234 | 225 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 80 | 18.5 | 18.0 | 403 | 390 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 90 | 21.3 | 20.9 | 602 | 584 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 100 | 23.7 | 23.2 | 827 | 805 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 110 | 25.8 | 25.2 | 1075 | 1047 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 120 | 27.7 | 27.0 | 1342 | 1309 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 130 | 29.3 | 28.6 | 1628 | 1587 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 140 | 18.9 | 19.9 | 1884 | 1840 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |
| 150 | 0.0 | 13.0 | 1979 | 2004 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |
| 160 | 0.0 | 6.2 | 1979 | 2102 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |
| 170 | 0.0 | 0.0 | 1979 | 2123 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |
| 180 | 0.0 | 0.0 | 1979 | 2123 | 82.0 | 84.0 | 56.8 | 54.0 | - | 84.0 | - | 54.0 |

- reached 10 m/s: TSRE 59.5 s, Open Rails 60.2 s
- reached 20 m/s: TSRE 85.5 s, Open Rails 87.2 s
- reached 30 m/s: TSRE never, Open Rails never
- reached 40 m/s: TSRE never, Open Rails never
- top speed: TSRE 29.35 m/s, Open Rails 28.61 m/s
- stopped: TSRE at 150.0 s after 351 m braking, Open Rails at 166.8 s after 528 m braking

## Throttle 0.6, then a light application (0.4)

Commands:

```
# USA1 morningmaryland, the player's HHP-8: brakes released, throttle 0.6 for 90 s, then a light graduated application (0.4).
0 direction 1
0 trainBrake 0
40 throttle 0.6
130 throttle 0
131 trainBrake 0.4
300 trainBrake 0.4
```

Neither slips nor slides: the braking distance within 2.2 %.

| s | TSRE m/s | OR m/s | TSRE m | OR m | TSRE pipe psi | OR pipe psi | TSRE cyl psi | OR cyl psi | TSRE last pipe | OR last pipe | TSRE last cyl | OR last cyl |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 30 | 0.0 | 0.0 | 0 | 0 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 40 | 0.0 | 0.0 | 0 | 0 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 50 | 5.3 | 5.0 | 27 | 24 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 60 | 10.5 | 10.2 | 105 | 100 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 70 | 15.1 | 14.8 | 234 | 226 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 80 | 18.5 | 18.2 | 403 | 391 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 90 | 21.3 | 21.0 | 602 | 588 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 100 | 23.7 | 23.3 | 827 | 809 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 110 | 25.8 | 25.3 | 1075 | 1053 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 120 | 27.7 | 27.1 | 1342 | 1315 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 130 | 29.3 | 28.6 | 1628 | 1594 | 110.0 | 110.0 | 0.0 | 0.0 | - | 110.0 | - | 0.0 |
| 140 | 23.4 | 23.2 | 1897 | 1860 | 98.5 | 99.0 | 23.3 | 27.0 | - | 99.0 | - | 27.0 |
| 150 | 14.8 | 15.4 | 2090 | 2053 | 98.5 | 99.0 | 23.3 | 27.0 | - | 99.0 | - | 27.0 |
| 160 | 4.5 | 6.5 | 2187 | 2164 | 98.5 | 99.0 | 23.3 | 27.0 | - | 99.0 | - | 27.0 |
| 170 | 0.0 | 0.0 | 2197 | 2185 | 98.5 | 99.0 | 23.3 | 27.0 | - | 99.0 | - | 27.0 |
| 180 | 0.0 | 0.0 | 2197 | 2185 | 98.5 | 99.0 | 23.3 | 27.0 | - | 99.0 | - | 27.0 |

- reached 10 m/s: TSRE 59.5 s, Open Rails 59.8 s
- reached 20 m/s: TSRE 85.5 s, Open Rails 86.7 s
- reached 30 m/s: TSRE never, Open Rails never
- reached 40 m/s: TSRE never, Open Rails never
- top speed: TSRE 29.35 m/s, Open Rails 28.70 m/s
- stopped: TSRE at 164.5 s after 569 m braking, Open Rails at 167.0 s after 582 m braking

## The light application released at 200 s

Commands:

```
# USA1 morningmaryland, the player's HHP-8: brakes released, throttle 0.6 for 90 s, then a light graduated application (0.4), released at 200 s.
0 direction 1
0 trainBrake 0
40 throttle 0.6
130 throttle 0
131 trainBrake 0.4
200 trainBrake 0
320 trainBrake 0
```

Twin pipe distributors: Open Rails recharges their reservoirs from the main reservoir pipe and releases in 10 s; TSRE, without that pipe, in minutes.

| s | TSRE m/s | OR m/s | TSRE m | OR m | TSRE pipe psi | OR pipe psi | TSRE cyl psi | OR cyl psi | TSRE last pipe | OR last pipe | TSRE last cyl | OR last cyl |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 190 | 0.0 | 0.0 | 2197 | 2176 | 98.5 | 99.0 | 23.3 | 27.0 | 98.5 | 99.0 | 28.7 | 27.0 |
| 200 | 0.0 | 0.0 | 2197 | 2176 | 98.5 | 99.0 | 23.3 | 27.0 | 98.5 | 99.0 | 28.7 | 27.0 |
| 210 | -0.0 | 0.0 | 2197 | 2176 | 99.9 | 110.0 | 20.1 | 0.0 | 99.7 | 110.0 | 28.7 | 0.0 |
| 220 | -0.0 | 0.0 | 2197 | 2176 | 101.2 | 110.0 | 17.4 | 0.0 | 101.0 | 110.0 | 22.5 | 0.0 |
| 230 | -0.0 | 0.0 | 2197 | 2176 | 102.6 | 110.0 | 14.7 | 0.0 | 102.4 | 110.0 | 19.1 | 0.0 |
| 240 | -0.0 | 0.0 | 2197 | 2176 | 103.9 | 110.0 | 11.9 | 0.0 | 103.7 | 110.0 | 15.8 | 0.0 |
| 250 | -0.0 | 0.0 | 2197 | 2176 | 105.2 | 110.0 | 9.3 | 0.0 | 105.0 | 110.0 | 12.4 | 0.0 |
| 260 | -0.0 | 0.0 | 2197 | 2176 | 106.5 | 110.0 | 6.8 | 0.0 | 106.3 | 110.0 | 9.3 | 0.0 |
| 270 | -0.0 | 0.0 | 2197 | 2176 | 107.6 | 110.0 | 4.6 | 0.0 | 107.4 | 110.0 | 6.5 | 0.0 |

- reached 10 m/s: TSRE 59.5 s, Open Rails 59.7 s
- reached 20 m/s: TSRE 85.5 s, Open Rails 86.7 s
- reached 30 m/s: TSRE never, Open Rails never
- reached 40 m/s: TSRE never, Open Rails never
- top speed: TSRE 29.35 m/s, Open Rails 28.66 m/s
- stopped: TSRE at 164.5 s after 569 m braking, Open Rails at 166.4 s after 577 m braking

## A 41-vehicle single pipe freight (USA2 nnmxdgds): a 13 psi reduction at 300 s, released at 360 s

Commands:

```
# USA2 nnmxdgds, three Dash 9s and 38 cars standing: released and charged, a 13 psi reduction at 300 s, released at 360 s, the recharge watched.
0 trainBrake 0
300 trainBrake 0.5
360 trainBrake 0
1200 trainBrake 0
```

Open Rails starts the train at its service's StartingSpeed (15 mph, throttle 25 %), TSRE standing: only the pressures compare (both standing from about 200 s). Both back to 90 psi at 570 to 600 s.

| s | TSRE m/s | OR m/s | TSRE m | OR m | TSRE pipe psi | OR pipe psi | TSRE cyl psi | OR cyl psi | TSRE last pipe | OR last pipe | TSRE last cyl | OR last cyl |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 270 | 0.0 | -0.7 | 271 | 794 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 300 | 0.0 | -1.0 | 271 | 769 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 330 | 0.0 | 0.0 | 271 | 763 | 77.0 | 78.0 | 32.5 | 31.0 | 77.0 | 78.0 | 32.2 | 31.0 |
| 360 | 0.0 | 0.0 | 271 | 763 | 77.0 | 78.0 | 32.5 | 31.0 | 77.0 | 78.0 | 32.2 | 31.0 |
| 390 | 0.0 | 0.0 | 271 | 763 | 80.6 | 86.0 | 0.0 | 0.0 | 78.5 | 85.0 | 32.2 | 0.0 |
| 420 | 0.0 | 0.0 | 271 | 763 | 82.5 | 87.0 | 0.0 | 0.0 | 80.9 | 86.0 | 0.0 | 0.0 |
| 450 | 0.0 | 0.0 | 271 | 763 | 84.7 | 88.0 | 0.0 | 0.0 | 83.1 | 87.0 | 0.0 | 0.0 |
| 480 | 0.0 | 0.0 | 271 | 763 | 86.6 | 88.0 | 0.0 | 0.0 | 85.2 | 88.0 | 0.0 | 0.0 |
| 510 | 0.0 | 0.0 | 271 | 763 | 88.2 | 89.0 | 0.0 | 0.0 | 87.1 | 88.0 | 0.0 | 0.0 |
| 540 | 0.0 | 0.0 | 271 | 763 | 89.3 | 89.0 | 0.0 | 0.0 | 88.5 | 89.0 | 0.0 | 0.0 |
| 570 | 0.0 | 0.0 | 271 | 763 | 89.9 | 90.0 | 0.0 | 0.0 | 89.5 | 89.0 | 0.0 | 0.0 |
| 600 | 0.0 | 0.0 | 271 | 763 | 90.0 | 90.0 | 0.0 | 0.0 | 89.9 | 90.0 | 0.0 | 0.0 |
| 630 | 0.0 | 0.0 | 271 | 763 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 660 | 0.0 | 0.0 | 271 | 763 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |

- reached 10 m/s: TSRE never, Open Rails never
- reached 20 m/s: TSRE never, Open Rails never
- reached 30 m/s: TSRE never, Open Rails never
- reached 40 m/s: TSRE never, Open Rails never
- top speed: TSRE 0.00 m/s, Open Rails 7.11 m/s
- stopped: TSRE at 237.0 s after 0 m braking, Open Rails at 189.3 s after 710 m braking

## After: the main reservoir pipe (the light application released at 200 s again)

Commands:

```
# USA1 morningmaryland, the player's HHP-8: brakes released, throttle 0.6 for 90 s, then a light graduated application (0.4), released at 200 s.
0 direction 1
0 trainBrake 0
40 throttle 0.6
130 throttle 0
131 trainBrake 0.4
200 trainBrake 0
320 trainBrake 0
```

TSRE with the main reservoir pipe: released as Open Rails, within 10 s. (The loco's own cylinder: 23.3 psi in TSRE from its file's TripleValveRatio 2.03; Open Rails' display shows 27.)

| s | TSRE m/s | OR m/s | TSRE m | OR m | TSRE pipe psi | OR pipe psi | TSRE cyl psi | OR cyl psi | TSRE last pipe | OR last pipe | TSRE last cyl | OR last cyl |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 195 | 0.0 | 0.0 | 2190 | 2176 | 98.5 | 99.0 | 23.3 | 27.0 | 98.5 | 99.0 | 29.2 | 27.0 |
| 200 | 0.0 | 0.0 | 2190 | 2176 | 98.5 | 99.0 | 23.3 | 27.0 | 98.5 | 99.0 | 29.2 | 27.0 |
| 205 | -0.0 | 0.0 | 2190 | 2176 | 107.0 | 106.9 | 6.6 | 8.2 | 106.9 | 106.9 | 8.6 | 8.2 |
| 210 | 0.0 | 0.0 | 2190 | 2176 | 109.9 | 110.0 | 0.0 | 0.0 | 110.0 | 110.0 | 0.0 | 0.0 |
| 215 | 0.0 | 0.0 | 2190 | 2176 | 110.1 | 110.0 | 0.0 | 0.0 | 110.2 | 110.0 | 0.0 | 0.0 |
| 220 | 0.0 | 0.0 | 2190 | 2176 | 110.1 | 110.0 | 0.0 | 0.0 | 110.2 | 110.0 | 0.0 | 0.0 |

- reached 10 m/s: TSRE 59.5 s, Open Rails 59.7 s
- reached 20 m/s: TSRE 85.5 s, Open Rails 86.7 s
- reached 30 m/s: TSRE never, Open Rails never
- reached 40 m/s: TSRE never, Open Rails never
- top speed: TSRE 29.35 m/s, Open Rails 28.66 m/s
- stopped: TSRE at 164.0 s after 563 m braking, Open Rails at 166.4 s after 577 m braking

## After: the service's StartingSpeed (the freight again)

Commands:

```
# USA2 nnmxdgds, three Dash 9s and 38 cars standing: released and charged, a 13 psi reduction at 300 s, released at 360 s, the recharge watched.
0 trainBrake 0
300 trainBrake 0.5
360 trainBrake 0
1200 trainBrake 0
```

Both start at 6.7 m/s with the throttle at 25 % and climb to a stand; the Dash 9s are TSRE's C++ stand-in, not a diesel model.

| s | TSRE m/s | OR m/s | TSRE m | OR m | TSRE pipe psi | OR pipe psi | TSRE cyl psi | OR cyl psi | TSRE last pipe | OR last pipe | TSRE last cyl | OR last cyl |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 0 | 6.7 | - | 0 | - | 90.0 | - | 0.0 | - | 90.0 | - | 0.0 | - |
| 15 | 7.1 | 7.1 | 111 | 104 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 30 | 7.3 | 7.0 | 217 | 210 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 45 | 7.0 | 6.5 | 325 | 312 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 60 | 6.5 | 5.9 | 426 | 405 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 75 | 5.9 | 5.3 | 519 | 489 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 90 | 5.3 | 4.7 | 602 | 565 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 105 | 4.8 | 4.1 | 678 | 632 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 120 | 4.2 | 3.5 | 746 | 689 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 135 | 3.6 | 2.8 | 804 | 736 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 150 | 2.9 | 2.0 | 853 | 772 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 165 | 2.2 | 1.3 | 892 | 797 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 180 | 1.5 | 0.5 | 920 | 811 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 195 | 0.7 | 0.0 | 936 | 814 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 210 | -0.0 | -0.0 | 941 | 813 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 225 | -0.4 | -0.1 | 944 | 812 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |
| 240 | -0.8 | -0.3 | 954 | 809 | 90.0 | 90.0 | 0.0 | 0.0 | 90.0 | 90.0 | 0.0 | 0.0 |

- reached 10 m/s: TSRE never, Open Rails never
- reached 20 m/s: TSRE never, Open Rails never
- reached 30 m/s: TSRE never, Open Rails never
- reached 40 m/s: TSRE never, Open Rails never
- top speed: TSRE 7.76 m/s, Open Rails 7.11 m/s
- stopped: TSRE at 208.5 s after 911 m braking, Open Rails at 189.3 s after 710 m braking

## The diesel (task 03): USA2's freight on diesel.lua

The same freight (three Dash 9s, 38 cars) now driven by `diesel.lua`
instead of the C++ stand-in, run again in Open Rails with its HUD's
locomotive and force pages recorded (`ORTS_HUD_PAGES="2 6"`). Commands:

```
# USA2 nnmxdgds, three Dash 9s and 38 cars: released, climbing at 25 % to a stand (300 s).
0 trainBrake 0
300 trainBrake 0
```

**The lead loco:**

| s | TSRE force kN | OR force kN | TSRE rpm | OR rpm |
| --- | --- | --- | --- | --- |
| 15 | 114.9 | 114.2 | 461 | 461 |
| 30 | 115.5 | 116.3 | 461 | 461 |
| 60 | 127.6 | 136.9 | 461 | 461 |
| 90 | 153.5 | 162.7 | 461 | 461 |
| 120 | 162.7 | 162.7 | 461 | 461 |
| 300 | 162.7 | 162.7 | 461 | 461 |

- **Both at 25 % throttle.** The engine is at 461 rpm, its throttle table's
  RPM. Each loco gives `MaxPower × t / v` (816 kW: 116 kN at 7 m/s), held
  to `MaxForce × t`, 162.7 kN, below 5 m/s.
- **Open Rails' HUD agrees on the engine:** 461 rpm, 1720 kW available, 52
  psi of oil, 95 °C.
- **Where the forces differ, the speeds differ:** Open Rails' train is
  slower, so its `P / v` is higher.

**The train:**
- It stops at 209 s in TSRE and 188 s in Open Rails, as with the stand-in
  (208 s). So the diesel is not the difference.
- **Open Rails' force page shows resistances TSRE does not model:**
  - curves: 14 to 27 kN over the train;
  - wind: up to 23 kN, a 41 km/h head wind.
- **TSRE's driven train has only its Davis resistance.**

## The diesel (task 03): BNSF_SCENIC's ES44AC

`3_CanadianImports`: three ES44ACs (one turned), 54 cars, 6668 t. Their
ENG has `ORTSDieselEngines` and force curves. Commands:

```
0 trainBrake 0
0 direction 1
10 throttle 0.5
150 throttle 1
300 throttle 0
360 throttle 0
```

**The lead loco:**

| s | TSRE force kN | OR force kN | TSRE rpm | OR rpm |
| --- | --- | --- | --- | --- |
| 30 | 282.9 | 358.7 | 593 | 588 |
| 60 | 206.1 | 210.3 | 682 | 682 |
| 90 | 159.3 | 156.3 | 682 | 682 |
| 120 | 136.9 | 132.7 | 682 | 682 |
| 150 | 123.5 | 118.0 | 682 | 682 |
| 180 | 208.7 | 202.4 | 1050 | 1050 |
| 210 | 190.7 | 176.4 | 1050 | 1050 |
| 240 | 178.9 | 157.7 | 1050 | 1050 |
| 270 | 173.0 | 145.3 | 1050 | 1050 |
| 300 | 167.4 | 134.0 | 1050 | 1050 |
| 330 | 0.0 | 0.0 | 622 | 623 |
| 360 | 0.0 | 0.0 | 315 | 315 |

- **The RPM agrees within 5 rpm,** rising and falling (the RPM law and the
  throttle table).
- **At full throttle each loco gives 2.80 MW in both:** 167.4 kN at
  16.8 m/s, against 134.0 kN at 20.7 m/s. All three pull in both.
- **At the start Open Rails' brakes are still releasing** (15 psi in the
  cylinders, the pipe at 86 psi, for 17 s); TSRE's start released.

**The train:** it reached 10 m/s at 113.0 s in TSRE and 108.6 s in Open
Rails. TSRE's top speed is 16.8 m/s, Open Rails' 20.8 m/s.

**Why: TSRE reads MSTS's `Friction` wrong** (task 01, `resistanceOf` in
`VehicleDefinition.cpp`). These cars have no ORTSDavis keys, so the Davis
coefficients come from the FCalc-style `Friction` block.
- **Open Rails fits by least squares,** from the second speed to 80 mph:
  `C1 × v1^E1 + C2 × v1 + C2 × v^E2`.
- **TSRE fits `C2 × v^(E2+1)`,** one power of speed too many.
- **The result:**
  - TSRE: 144 kN of Davis resistance over the train at 10 m/s, 722 kN at
    20 m/s;
  - Open Rails' friction: about 90 kN at 20 m/s;
  - the ES44AC's own coefficients come out as A 25.6 kN, B −7.7 kN/(m/s),
    C 595 N/(m/s)².
- **With Open Rails' form** (tried, not committed):

  | | TSRE | Open Rails |
  | --- | --- | --- |
  | Reaches 10 m/s | 108.0 s | 108.6 s |
  | Reaches 20 m/s | 272 s | 284 s |
  | Top speed | 21.4 m/s | 20.8 m/s |

  The small lead left fits TSRE's missing curve and wind resistance.
- **The same coefficients feed the AI's performance** (`PerformanceOf`):
  the fix changes AI traces too. The user's decision.
