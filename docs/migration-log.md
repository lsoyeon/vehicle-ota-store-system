# Migration Log

## 2026-06-08

The archive repository started as an empty `main` branch. An empty initial commit
was created to provide a merge base:

```sh
git commit --allow-empty -m "Initialize HAMES archive repository"
```

Each source repository was imported with this process:

```sh
git clone --single-branch --no-tags <source-url> <temporary-directory>
git -C <temporary-directory> filter-repo --to-subdirectory-filter src/<target-folder> --force
git remote add import-<name> <temporary-directory>
git fetch import-<name> <branch>
git merge --allow-unrelated-histories --no-ff -m "Import <target-folder> history" import-<name>/<branch>
git remote remove import-<name>
```

## Verification

- Confirmed all eight target folders exist under `src/`.
- Confirmed path-scoped Git history is available, for example:

```sh
git log --oneline -- src/firmware-motor-ecu
```

- Confirmed imported `.gitattributes` files did not contain Git LFS filter
  patterns.
- Confirmed no temporary import remotes remain configured in the archive
  repository.

## Temporary Workspace

The import clones were created under:

```text
/private/tmp/hames-archive-import.uFiybn
```
