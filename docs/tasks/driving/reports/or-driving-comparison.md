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
