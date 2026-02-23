# UI bench summary

| | |
| --- | --- |
| commit | `4f4af641ad4c3662332ef42d468768e690c05c7f` |
| dirty | false |
| config | RelWithDebInfo |
| engine | 0.1.0a-dev (build 1541b3f5d2b44f76) |
| window | 1600 x 900, vsync false |
| frames | warmup 60, measured 120, repeat 5 (median of each average) |
| date | 2026-10-14T22:36:02Z |
| machine | Intel(R) Xeon(R) Processor @ 2.10GHz (4 threads); software GL (llvmpipe); Ubuntu 24.04.5 LTS 6.18.44-fc-v114; power plan n/a |

Milliseconds are CPU averages per frame. `total` = the six stages + `commands`; `spread` = (max - min) / median of `total` across runs; `peak` = the sum of each stage's max, an upper bound of the worst frame. `state` = save, restore, scissor, transform, view, opacity, font calls; `top` = the three most frequent drawing calls. `layout` marks `skip` when the last frame skipped layout. Every scene has one canvas unless `canvases` says otherwise.

| row | elements | generated | bindings | styles | input | layout | motion | paint | commands | total | spread | peak | draws | state | top |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| table/quiet | 244 | 38 | 0.054 | 0.001 | 0.000 | 0.000 skip | 0.025 | 0.277 | 0.021 | 0.382 | 13.9% | 1.329 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| table/one-change | 244 | 38 | 0.050 | 0.001 | 0.000 | 0.115 | 0.023 | 0.271 | 0.023 | 0.484 | 11.8% | 0.865 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| table/hover/paint | 244 | 38 | 0.030 | 0.001 | 0.094 | 0.000 skip | 0.068 | 0.286 | 0.017 | 0.494 | 40.9% | 1.281 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| table/hover/layout | 244 | 38 | 0.029 | 0.001 | 0.089 | 0.000 skip | 0.066 | 0.283 | 0.018 | 0.497 | 11.9% | 0.898 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| table/scroll | 257 | 41 | 0.078 | 0.001 | 0.099 | 0.110 skip | 0.084 | 0.294 | 0.018 | 0.690 | 16.5% | 2.347 | 297.3 | 1218.7 | text 204.7, fill_rect 43.3, fill_rounded_rect 2 |
| table/churn | 244 | 38 | 0.139 | 0.001 | 0.000 | 0.223 | 0.115 | 0.275 | 0.022 | 0.780 | 13.6% | 2.152 | 278 | 1144 | text 193, fill_rect 40, fill_rounded_rect 2 |
| inspector/quiet | 2926 | 395 | 0.282 | 0.001 | 0.000 | 0.000 skip | 0.192 | 1.164 | 0.200 | 1.839 | 15.3% | 3.079 | 1707 | 4999 | text 356, fill_rect 258, fill_rounded_rect 246 |
| inspector/one-change | 2926 | 395 | 0.287 | 0.001 | 0.000 | 1.417 | 0.190 | 1.148 | 0.205 | 3.261 | 11.5% | 6.989 | 1707 | 4999 | text 356, fill_rect 258, fill_rounded_rect 246 |
| inspector/hover/paint | 2926 | 395 | 0.221 | 0.001 | 0.552 | 0.000 skip | 0.236 | 1.274 | 0.212 | 2.495 | 11.5% | 4.720 | 1707 | 4999 | text 356, fill_rect 258, fill_rounded_rect 246 |
| inspector/hover/layout | 2926 | 395 | 0.214 | 0.001 | 0.529 | 0.000 skip | 0.216 | 1.171 | 0.204 | 2.335 | 7.2% | 7.412 | 1707 | 4999 | text 356, fill_rect 258, fill_rounded_rect 246 |
| inspector/scroll | 2928 | 395 | 0.221 | 0.001 | 0.530 | 0.405 skip | 0.210 | 1.174 | 0.194 | 2.719 | 11.2% | 8.985 | 1717.2 | 5024.4 | text 358.6, fill_rect 257.4, fill_rounded_rect 248.2 |
| inspector/churn | 2926 | 395 | 0.379 | 0.001 | 0.000 | 1.438 | 0.282 | 1.103 | 0.204 | 3.408 | 19.1% | 6.470 | 1673.5 | 4897.8 | text 349.8, fill_rect 252, fill_rounded_rect 241.8 |
| hud/quiet | 184 | 32 | 0.031 | 0.001 | 0.000 | 0.000 skip | 0.039 | 0.428 | 0.022 | 0.516 | 5.6% | 0.995 | 1995 | 873 | fill_rect 808, text 123, line 64 |
| hud/one-change | 184 | 32 | 0.027 | 0.001 | 0.000 | 0.125 | 0.030 | 0.400 | 0.024 | 0.601 | 22.0% | 1.355 | 1995 | 873 | fill_rect 808, text 123, line 64 |
| hud/hover/paint | 184 | 32 | 0.007 | 0.001 | 0.071 | 0.000 skip | 0.067 | 0.424 | 0.019 | 0.589 | 17.0% | 1.127 | 1996.6 | 873 | fill_rect 808.8, text 123, line 64 |
| hud/hover/layout | 184 | 32 | 0.007 | 0.001 | 0.063 | 0.000 skip | 0.062 | 0.427 | 0.017 | 0.576 | 11.0% | 1.402 | 1995 | 873 | fill_rect 808, text 123, line 64 |
| paint-mix/quiet/solid | 1083 | 1025 | 0.069 | 0.001 | 0.000 | 0.000 skip | 0.084 | 0.458 | 0.051 | 0.665 | 4.6% | 1.227 | 2003 | 4120 | fill_rect 1001, text 1 |
| paint-mix/quiet/rounded | 1083 | 1025 | 0.061 | 0.001 | 0.000 | 0.000 skip | 0.086 | 1.038 | 0.051 | 1.245 | 15.5% | 2.168 | 2003 | 4120 | fill_rounded_rect 1000, fill_rect 1, text 1 |
| paint-mix/quiet/border | 1083 | 1025 | 0.060 | 0.001 | 0.000 | 0.000 skip | 0.086 | 0.518 | 0.051 | 0.720 | 13.0% | 1.224 | 3003 | 4120 | stroke_rect 1000, fill_rect 1, text 1 |
| paint-mix/quiet/linear-gradient | 1083 | 1025 | 0.056 | 0.001 | 0.000 | 0.000 skip | 0.089 | 0.538 | 0.051 | 0.745 | 26.6% | 1.284 | 2003 | 4120 | linear_gradient 1000, fill_rect 1, text 1 |
| paint-mix/quiet/radial-gradient | 1083 | 1025 | 0.070 | 0.001 | 0.000 | 0.000 skip | 0.088 | 0.547 | 0.052 | 0.755 | 8.2% | 1.739 | 2003 | 4120 | radial_gradient 1000, fill_rect 1, text 1 |
| paint-mix/quiet/conic-gradient | 1083 | 1025 | 0.071 | 0.001 | 0.000 | 0.000 skip | 0.091 | 5.884 | 0.052 | 6.090 | 18.0% | 9.888 | 2003 | 4120 | conic_gradient 1000, fill_rect 1, text 1 |
| paint-mix/quiet/image | 1083 | 1025 | 0.068 | 0.001 | 0.000 | 0.000 skip | 0.085 | 0.527 | 0.054 | 0.738 | 15.0% | 1.411 | 2003 | 4120 | image 1000, text 1, fill_rect 1 |
| paint-mix/quiet/nine-slice | 1083 | 1025 | 0.086 | 0.001 | 0.000 | 0.000 skip | 0.108 | 2.587 | 0.070 | 2.851 | 23.3% | 5.159 | 18003 | 4120 | nine_slice 1000, text 1, fill_rect 1 |
| paint-mix/quiet/text | 1083 | 1025 | 0.069 | 0.000 | 0.000 | 0.000 skip | 0.090 | 1.087 | 0.056 | 1.295 | 10.3% | 2.480 | 1003 | 5120 | text 1001, fill_rect 1 |
| paint-mix/quiet/arc | 1083 | 1025 | 0.078 | 0.000 | 0.000 | 0.000 skip | 0.084 | 1.310 | 0.055 | 1.521 | 15.8% | 2.989 | 3003 | 4120 | arc 1000, fill_rect 1, text 1 |
| paint-mix/quiet/math | 1083 | 1025 | 0.112 | 0.001 | 0.000 | 0.000 skip | 0.143 | 7.876 | 0.145 | 8.276 | 11.5% | 14.315 | 9003 | 4120 | path 1000, fill_rect 1, text 1 |
| motion/quiet/paint-props | 261 | 230 | 0.022 | 0.001 | 0.000 | 0.000 skip | 0.806 | 0.313 | 0.019 | 1.176 | 6.4% | 3.290 | 443 | 1144 | fill_rounded_rect 220, fill_rect 1, text 1 |
| motion/quiet/layout-props | 261 | 230 | 0.022 | 0.001 | 0.000 | 0.048 | 0.065 | 0.235 | 0.019 | 0.392 | 14.7% | 0.937 | 443 | 944 | fill_rounded_rect 220, fill_rect 1, text 1 |
| motion/quiet/both | 261 | 230 | 0.020 | 0.001 | 0.000 | 0.048 | 0.795 | 0.292 | 0.018 | 1.159 | 16.9% | 3.447 | 443 | 1144 | fill_rounded_rect 220, fill_rect 1, text 1 |
| text/quiet | 276 | 260 | 0.034 | 0.000 | 0.000 | 0.000 skip | 0.023 | 4.659 | 0.023 | 4.740 | 8.3% | 8.324 | 452 | 1336 | text 448, fill_rounded_rect 1, fill_rect 1 |
| text/scroll | 276 | 260 | 0.014 | 0.000 | 0.084 | 0.000 skip | 0.028 | 4.639 | 0.020 | 4.786 | 12.6% | 7.378 | 452 | 1339 | text 448, fill_rounded_rect 1, fill_rect 1 |
| clip/quiet | 279 | 208 | 0.037 | 0.000 | 0.000 | 0.000 skip | 0.026 | 0.778 | 0.024 | 0.867 | 7.6% | 1.579 | 300 | 1109 | text 185, fill_rect 25, fill_rounded_rect 11 |
| clip/scroll | 323 | 246 | 0.077 | 0.000 | 0.093 | 0.094 skip | 0.034 | 0.924 | 0.023 | 1.248 | 8.9% | 3.727 | 345.3 | 1274.3 | text 213.4, fill_rect 28.4, fill_rounded_rect 12.7 |
