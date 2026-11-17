# Assets

Design files for the project.

- `figma/` holds Figma exports (`.fig`) and images exported from Figma.
- `3d/` holds 3D models and scenes (`.blend`, `.fbx`, `.obj`, `.glb`, `.usdz`, `.stl`).

Binary files in this folder go through Git LFS, so install it once before you add any:

```sh
brew install git-lfs   # or see https://git-lfs.com
git lfs install
```

Then add and push files as usual. `.gitattributes` in the repo root lists the tracked file types. If you need a new type, add a line there.
