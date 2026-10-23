# Asset provenance

Original game, level design, characters and artwork: **Rovio**. Native calculator adaptation: **Bobydeluxe**. UI font: **Nunito**, reused from NumPlay's generated font tables under the [SIL Open Font License](../../LICENSES/OFL-1.1-Nunito.txt). Asset ownership remains with the original owners; the repository's code license does not relicense third-party artwork.

Reference supplied by the user: [Gr4ys0n Angry Birds directory](https://github.com/Gr4ys0n/Gr4ys0n.github.io/tree/29006b0f11904704c390b457f7055c9fa1e53484/public/assets/games/angry-birds), pinned to commit `29006b0f11904704c390b457f7055c9fa1e53484`. Inputs were taken from `cors/fowl/`: level/shape/material/episode JSON, sprite sheets, graphical font, menu artwork, background layers and cutscene images. The original GWT JavaScript was consulted as a behavior reference; it is not embedded or executed by the calculator app.

The converted input set is self-contained:

| File | Purpose |
| --- | --- |
| `assets/source.json.zlib` | Original level/object/shape/material data, campaign groups and story triggers |
| `assets/atlas.png` | Converted RGBA artwork atlas |
| `assets/atlas.json` | Sprite bounds, pivots and dimensions |
| `assets/mapping.json` | Object frames and background mapping |
| `assets/provenance.json` | Source revision, per-level normalized JSON hashes and converted-file SHA-256 hashes |

`tools/import_reference.py --source /path/to/cors/fowl` reconstructs these inputs from an extracted copy of the pinned reference. It does not download or execute source code. Routine builds use only the converted checked-in files through `tools/assets.py`.

Conversion keeps all 443 playable maps and original float32 placements. It excludes the benchmark. Artwork is resized for the calculator, cropped from the source sheets and stored in per-sprite 15-color palettes plus transparency. The title uses the classic wordmark component extracted from the source title artwork, without the detached Beta badge or Chrome logo. The old resized headline-font sprites are not packed; UI text uses native-size Nunito coverage tables from `launcher/src/font_data.c`. The backgrounds use fixed-size composites; the 19 original cutscene pictures are shown as static storyboards. The slingshot is drawn procedurally. Sound, external comics and browser/service assets are not bundled.

`tests/test_assets.py` verifies the converted hashes, level counts, lossless level-stream decoding, float32 placement preservation and deterministic generated C output. Reproducing the exact atlas/ROM conversion should use the recorded Pillow 11.3.0 version.
