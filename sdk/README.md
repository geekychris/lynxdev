# lynxdev SDK

The library every project links (reference: [docs/sdk.md](../docs/sdk.md)).

## Versioning

`LYNX_SDK_VERSION` (sdk/include/lynxhw.h, included by every SDK header) is
`major * 10000 + minor * 100 + patch`.

- **patch**: fixes, faster code, docs. No API change.
- **minor**: additions only (new functions, headers, macros). Every existing
  program builds and behaves the same.
- **major**: a change that can break a program (renamed or removed function,
  changed meaning, memory map change). Only with the platform owner's
  agreement and every project in the repo updated in the same PR.

```c
#if LYNX_SDK_VERSION < 10100
#error needs lynxdev SDK 1.1
#endif
```

Changes to `sdk/` go through a PR that keeps `./lynx selftest` green.

## Changelog

- **1.2.0**: cheaper sound: `snd_tone` ~1.3k cycles (was ~6.4k; a sweep was a
  32-bit software divide every frame), `snd_period` on Suzy's divider in asm
  (~170), `SND_PERIOD(hz)` compile-time periods + `snd_tone_p` / `snd_freq_p`.
  `spr_box` (exact w x h boxes as sprites). Log: `lynx_log_s` (~20 cycles a
  character, was ~150) and `lynx_log_u` (~160, was ~1.3k) in asm. `math_mul16`
  / `math_div32` ~200-270 cycles (were ~1.2k). Same API.

- **1.1.0**: lynxtext.h (3x5 Suzy font, 40 columns), lynxmath.h (Suzy multiply/divide,
  `box_hit`).

- **1.0.0**: first release: lynx.cfg / lynx-coll.cfg memory maps, project.mk,
  log ($FCF0/$FCF1, compiled out with RELEASE=1), TGI-based screen + frame
  counter, palette, shapes, text, Suzy sprites (lynxspr.h), input, Mikey sound
  effects, rnd, template.
