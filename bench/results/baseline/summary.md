# UI bench summary

| | |
| --- | --- |
| commit | `e7211f300e6a4a7d9b65664a42c3185ca0917f8c` |
| dirty | true |
| config | RelWithDebInfo |
| engine | 0.1.0 (build e89d200b03110a98) |
| window | 1600 x 900, vsync false |
| frames | warmup 60, measured 120, repeat 3 (median of each average) |
| date | 2026-10-09T19:38:58Z |
| machine | AMD Ryzen 5 5600H with Radeon Graphics; NVIDIA GeForce GTX 1650; AMD Radeon(TM) Graphics; Microsoft Windows 10 Home; power plan Balanced |

Milliseconds are CPU averages per frame. `total` = the six stages + `commands`; `spread` = (max - min) / median of `total` across runs; `peak` = the sum of each stage's max, an upper bound of the worst frame. `state` = save, restore, scissor, transform, view, opacity, font calls; `top` = the three most frequent drawing calls. `layout` marks `skip` when the last frame skipped layout. Every scene has one canvas unless `canvases` says otherwise.

| row | elements | generated | bindings | styles | input | layout | motion | paint | commands | total | spread | peak | draws | state | top |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| table/quiet | 244 | 38 | 0.040 | 0.001 | 0.000 | 0.000 skip | 0.034 | 0.351 | 0.016 | 0.441 | 8.9% | 0.894 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| table/one-change | 244 | 38 | 0.057 | 0.001 | 0.000 | 0.165 | 0.037 | 0.390 | 0.019 | 0.668 | 28.8% | 1.578 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| table/hover/paint | 244 | 38 | 0.028 | 0.001 | 0.080 | 0.000 skip | 0.079 | 0.372 | 0.015 | 0.572 | 3.8% | 1.117 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| table/hover/layout | 244 | 38 | 0.027 | 0.001 | 0.078 | 0.000 skip | 0.079 | 0.366 | 0.015 | 0.569 | 3.0% | 1.270 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| table/scroll | 257 | 41 | 0.099 | 0.001 | 0.093 | 0.173 skip | 0.136 | 0.409 | 0.018 | 0.928 | 8.7% | 3.135 | 297.3 | 1218.7 | text 204.7, fill_rect 43.3, fill_rounded_rect 2 |
| table/churn | 244 | 38 | 0.190 | 0.001 | 0.000 | 0.307 | 0.181 | 0.386 | 0.021 | 1.086 | 15.8% | 2.445 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| inspector/quiet | 2926 | 395 | 0.753 | 0.002 | 0.000 | 0.000 skip | 0.405 | 1.776 | 0.275 | 3.195 | 9.9% | 4.781 | 1707 | 4999 | text 356, fill_rect 258, fill_rounded_rect 246 |
| inspector/one-change | 2926 | 395 | 0.775 | 0.002 | 0.000 | 1.819 | 0.407 | 1.774 | 0.292 | 5.071 | 9.7% | 7.625 | 1707 | 4999 | text 356, fill_rect 258, fill_rounded_rect 246 |
| inspector/hover/paint | 2926 | 395 | 0.409 | 0.001 | 1.201 | 0.000 skip | 0.445 | 1.780 | 0.254 | 4.091 | 39.5% | 6.630 | 1707 | 4999 | text 356, fill_rect 258, fill_rounded_rect 246 |
| inspector/hover/layout | 2926 | 395 | 0.361 | 0.002 | 1.221 | 0.000 skip | 0.427 | 1.748 | 0.253 | 4.016 | 12.3% | 6.767 | 1707 | 4999 | text 356, fill_rect 258, fill_rounded_rect 246 |
| inspector/scroll | 2928 | 395 | 0.401 | 0.002 | 1.227 | 0.453 skip | 0.422 | 1.814 | 0.245 | 4.576 | 6.1% | 9.373 | 1717.2 | 5024.4 | text 358.6, fill_rect 257.4, fill_rounded_rect 248.2 |
| inspector/churn | 2926 | 395 | 0.953 | 0.002 | 0.000 | 1.953 | 0.534 | 1.787 | 0.305 | 5.565 | 7.2% | 9.384 | 1673.5 | 4897.8 | text 349.8, fill_rect 252, fill_rounded_rect 241.8 |
| hud/quiet | 184 | 32 | 0.026 | 0.001 | 0.000 | 0.000 skip | 0.042 | 0.533 | 0.018 | 0.619 | 3.4% | 1.275 | 1995 | 873 | fill_rect 808, text 123, line 64 |
| hud/one-change | 184 | 32 | 0.028 | 0.001 | 0.000 | 0.147 | 0.040 | 0.528 | 0.017 | 0.759 | 9.0% | 1.538 | 1995 | 873 | fill_rect 808, text 123, line 64 |
| hud/hover/paint | 184 | 32 | 0.013 | 0.001 | 0.068 | 0.000 skip | 0.075 | 0.567 | 0.016 | 0.739 | 11.5% | 1.778 | 1996.6 | 873 | fill_rect 808.8, text 123, line 64 |
| hud/hover/layout | 184 | 32 | 0.013 | 0.001 | 0.079 | 0.000 skip | 0.079 | 0.572 | 0.021 | 0.764 | 13.1% | 1.952 | 1995 | 873 | fill_rect 808, text 123, line 64 |
| paint-mix/quiet/solid | 1083 | 1025 | 0.084 | 0.002 | 0.000 | 0.000 skip | 0.268 | 0.878 | 0.069 | 1.302 | 21.5% | 2.579 | 2003 | 4120 | fill_rect 1001, text 1 |
| paint-mix/quiet/rounded | 1083 | 1025 | 0.088 | 0.002 | 0.000 | 0.000 skip | 0.254 | 1.598 | 0.089 | 2.030 | 17.1% | 3.945 | 2003 | 4120 | fill_rounded_rect 1000, text 1, fill_rect 1 |
| paint-mix/quiet/border | 1083 | 1025 | 0.078 | 0.002 | 0.000 | 0.000 skip | 0.220 | 0.866 | 0.085 | 1.246 | 18.8% | 2.724 | 3003 | 4120 | stroke_rect 1000, text 1, fill_rect 1 |
| paint-mix/quiet/linear-gradient | 1083 | 1025 | 0.068 | 0.001 | 0.000 | 0.000 skip | 0.208 | 0.845 | 0.058 | 1.179 | 4.0% | 2.500 | 2003 | 4120 | linear_gradient 1000, text 1, fill_rect 1 |
| paint-mix/quiet/radial-gradient | 1083 | 1025 | 0.068 | 0.001 | 0.000 | 0.000 skip | 0.214 | 0.849 | 0.056 | 1.192 | 8.4% | 2.299 | 2003 | 4120 | radial_gradient 1000, text 1, fill_rect 1 |
| paint-mix/quiet/conic-gradient | 1083 | 1025 | 0.121 | 0.003 | 0.000 | 0.000 skip | 0.319 | 13.756 | 0.115 | 14.314 | 3.4% | 20.898 | 2003 | 4120 | conic_gradient 1000, text 1, fill_rect 1 |
| paint-mix/quiet/image | 1083 | 1025 | 0.075 | 0.002 | 0.000 | 0.000 skip | 0.223 | 0.898 | 0.058 | 1.256 | 29.1% | 2.574 | 2003 | 4120 | image 1000, fill_rect 1, text 1 |
| paint-mix/quiet/nine-slice | 1083 | 1025 | 0.184 | 0.003 | 0.000 | 0.000 skip | 0.319 | 3.511 | 0.143 | 4.161 | 2.0% | 6.478 | 18003 | 4120 | nine_slice 1000, fill_rect 1, text 1 |
| paint-mix/quiet/text | 1083 | 1025 | 0.084 | 0.001 | 0.000 | 0.000 skip | 0.250 | 1.746 | 0.083 | 2.162 | 6.3% | 3.941 | 1003 | 5120 | text 1001, fill_rect 1 |
| paint-mix/quiet/arc | 1083 | 1025 | 0.421 | 0.001 | 0.000 | 0.000 skip | 0.244 | 2.022 | 0.094 | 2.781 | 15.5% | 4.767 | 3003 | 4120 | arc 1000, text 1, fill_rect 1 |
| paint-mix/quiet/math | 1083 | 1025 | 0.192 | 0.003 | 0.000 | 0.000 skip | 0.330 | 10.303 | 0.183 | 11.002 | 4.6% | 14.252 | 9003 | 4120 | path 1000, text 1, fill_rect 1 |
| motion/quiet/paint-props | 261 | 230 | 0.020 | 0.001 | 0.000 | 0.000 skip | 1.053 | 0.451 | 0.017 | 1.545 | 1.3% | 3.201 | 443 | 1144 | fill_rounded_rect 220, text 1, fill_rect 1 |
| motion/quiet/layout-props | 261 | 230 | 0.017 | 0.001 | 0.000 | 0.113 | 0.095 | 0.341 | 0.015 | 0.586 | 13.5% | 1.472 | 443 | 944 | fill_rounded_rect 220, text 1, fill_rect 1 |
| motion/quiet/both | 261 | 230 | 0.020 | 0.001 | 0.000 | 0.117 | 1.112 | 0.452 | 0.017 | 1.723 | 2.8% | 3.007 | 443 | 1144 | fill_rounded_rect 220, text 1, fill_rect 1 |
| text/quiet | 276 | 260 | 0.189 | 0.002 | 0.000 | 0.000 skip | 0.090 | 6.072 | 0.045 | 6.396 | 4.7% | 9.381 | 452 | 1336 | text 448, fill_rect 1, fill_rounded_rect 1 |
| text/scroll | 276 | 260 | 0.079 | 0.002 | 0.318 | 0.000 skip | 0.097 | 6.094 | 0.049 | 6.632 | 7.6% | 10.457 | 452 | 1339 | text 448, fill_rect 1, fill_rounded_rect 1 |
| clip/quiet | 279 | 208 | 0.059 | 0.001 | 0.000 | 0.000 skip | 0.040 | 1.071 | 0.024 | 1.195 | 40.6% | 2.078 | 300 | 1109 | text 185, fill_rect 25, fill_rounded_rect 11 |
| clip/scroll | 323 | 246 | 0.140 | 0.001 | 0.138 | 0.147 skip | 0.057 | 1.205 | 0.028 | 1.716 | 17.9% | 4.644 | 345.3 | 1274.3 | text 213.4, fill_rect 28.4, fill_rounded_rect 12.7 |
