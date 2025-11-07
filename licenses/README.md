# Licenses Bundle

This directory contains the full license texts that must accompany Megatoy
executables and installers. Keep the entire `licenses/` folder next to any
redistributed binary so recipients can review the original terms for Megatoy
and all third-party components.

- `megatoy-MIT.txt` – Megatoy’s own MIT License (same as `LICENSES.md`).
- Files prefixed with a dependency name (for example,
  `nlohmann-json-MIT.txt`, `nuked-opn2-LGPL-2.1.txt`) reproduce the exact
  upstream license for that dependency.
- `font-awesome-license.txt` and `presets-CC0.txt` duplicate the asset
  licenses bundled under `assets/`.

If you add another dependency, please drop its license text in this folder and
update `THIRD_PARTY_NOTICES.md` accordingly.
