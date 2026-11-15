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

- **1.1.0**: lynxtext.h (3x5 Suzy font, 40 columns), lynxmath.h (Suzy multiply/divide,
  `box_hit`).

- **1.0.0**: first release: lynx.cfg / lynx-coll.cfg memory maps, project.mk,
  log ($FCF0/$FCF1, compiled out with RELEASE=1), TGI-based screen + frame
  counter, palette, shapes, text, Suzy sprites (lynxspr.h), input, Mikey sound
  effects, rnd, template.
