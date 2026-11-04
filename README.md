# PsyRoot

Psytrance bass instrument. AU + VST3, macOS. Product name **PsyRoot**. This repository is BassSynth.

Coordination lives in ChatBoxx `#basssynth`.

## Branches

`main` is the only long-lived branch, and it is what we build. Changes land as squash-merged pull requests. Do not push to `main` after this skeleton.

| Branch | Who | Owns |
|--------|-----|------|
| `fable/engine-arch-v0.1` | Fable | `docs/ARCHITECTURE_*` |
| `astra/ui-v2` | Astra | `docs/ui/**` and design assets |
| `cursor/<feature>` | Cursor | `Source/**`, CMake, CI |

Rebase onto `main` before merge. No force-push to `main`.

## Docs

See [docs/README.md](docs/README.md).

## Identity

These stay put if the product is renamed again. The display string is `PsyRoot`.

| Field | Value |
|-------|--------|
| Bundle ID | `com.42codon.basssynth` |
| Plugin code | `Bs01` |
| Manufacturer code | `Cdon` (42codon-wide; Vizuator keeps `Vizu`) |
| Company | `42codon` |

## Build

JUCE 8.0.12 via the symlink `ThirdParty/JUCE` → `../../MyJUCE`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target BassSynthTests -j8
./build/BassSynthTests
cmake --build build --target BassSynth_All -j8
```

Distribution and CI pass `-DBASSSYNTH_DEV_PATHS=OFF`. The plugin reports 0 samples of latency until oversampling exists. Parameter IDs are not in this tree yet. The sidechain input is stereo and off until a host enables it.
