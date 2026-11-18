# STKickstarter

## Assets

Design files live in [`assets/`](assets/):

- [`assets/figma/`](assets/figma/) has Figma files and exported images.
- [`assets/3d/`](assets/3d/) has 3D models.

We work on the designs together in the [Figma file](https://www.figma.com/design/ZTAxM9YLRO8PUJfTeC3x9O/SH-KickStarter?node-id=0-1). See [`assets/README.md`](assets/README.md) before you add files.

## App

The web app lives in [`frontend/`](frontend/). It's an Expo (React Native) app that we only ship for the web. The backend is [Supabase](https://supabase.com), with the database schema in [`supabase/migrations/`](supabase/migrations/).

You need Docker and the [Supabase CLI](https://supabase.com/docs/guides/local-development/cli/getting-started) for the local backend. From the repo root:

```sh
supabase start
```

This runs the backend on port 55321, so it doesn't clash with other local Supabase projects. Copy `frontend/.env.example` to `frontend/.env.local` and fill in the publishable key that `supabase status` prints. Then start the app:

```sh
cd frontend
npm install
npm run web
```

This opens http://localhost:8082. Saving a file updates the page without a full reload.

### Live version

The test version runs at https://stkickstarter.expo.app on EAS Hosting, against the hosted Supabase project `stkickstarter`. To publish a new version, run `npm run deploy` in `frontend/` (you need to be logged in with `npx eas-cli login`). New migrations aren't applied to the hosted database automatically yet, so apply them before deploying code that needs them.
