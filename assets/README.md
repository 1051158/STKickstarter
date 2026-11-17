# Assets

Design files for the project.

- `figma/` holds Figma exports (`.fig`) and images exported from the [Figma file](https://www.figma.com/design/ZTAxM9YLRO8PUJfTeC3x9O/SH-KickStarter?node-id=0-1). We discuss the designs in the [Teams chat](https://teams.microsoft.com/l/message/19:6740adc7075f4e09a7efbddb4d62bd6a@thread.v2/1794915233021?context=%7B%22contextType%22%3A%22chat%22%7D).
- `3d/` holds 3D models and scenes (`.blend`, `.fbx`, `.obj`, `.glb`, `.usdz`, `.stl`).

Binary files in this folder go through Git LFS, so install it once before you add any:

```sh
brew install git-lfs   # or see https://git-lfs.com
git lfs install
```

Then add and push files as usual. `.gitattributes` in the repo root lists the tracked file types. If you need a new type, add a line there.
