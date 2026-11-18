# STKickstarter

## Assets

Design files live in [`assets/`](assets/):

- [`assets/figma/`](assets/figma/) has Figma files and exported images.
- [`assets/3d/`](assets/3d/) has 3D models.

We work on the designs together in the [Figma file](https://www.figma.com/design/ZTAxM9YLRO8PUJfTeC3x9O/SH-KickStarter?node-id=0-1). See [`assets/README.md`](assets/README.md) before you add files.

## App

The web app lives in [`frontend/`](frontend/). It's an Expo (React Native) app that we only ship for the web.

```sh
cd frontend
npm install
npm run web
```

This opens http://localhost:8082. Saving a file updates the page without a full reload.
