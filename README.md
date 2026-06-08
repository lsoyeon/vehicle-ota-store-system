# HAMES 6th Overdrive Archive

This repository consolidates selected HAMES 6th Overdrive repositories into a
single history-preserving archive.

Each source repository was imported under `src/<project-folder>/` with its
default branch history rewritten into that subdirectory. Original project
README and documentation files remain inside each imported project folder.

## Projects

| Project folder | Source repository | Imported branch |
| --- | --- | --- |
| `src/firmware-ota-bootloader/` | `HAMES-6th-Overdrive/bootloader` | `main` |
| `src/firmware-motor-ecu/` | `HAMES-6th-Overdrive/motor-ecu` | `main` |
| `src/firmware-camera-ecu/` | `HAMES-6th-Overdrive/camera-ecu` | `main` |
| `src/feature-fvsa/` | `HAMES-6th-Overdrive/FVSA` | `main` |
| `src/feature-lkas/` | `HAMES-6th-Overdrive/LKAS` | `main` |
| `src/firmware-front-zcu/` | `HAMES-6th-Overdrive/firmware-front-zcu` | `main` |
| `src/firmware-sensor-ecu/` | `HAMES-6th-Overdrive/sensor-ecu` | `main` |
| `src/firmware-vehicle-computer/` | `HAMES-6th-Overdrive/vehicle-computer` | `main` |

## Layout

- `src/`: imported project source trees.
- `docs/repositories.md`: source URL, target path, branch, and import commit index.
- `docs/migration-log.md`: migration procedure and verification notes.

## History Lookup

Use path-scoped Git history to inspect a specific imported project:

```sh
git log --oneline -- src/firmware-motor-ecu
```

Because each source history was rewritten into its archive path before merging,
path-scoped history remains available for each imported folder.
