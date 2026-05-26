import { defineConfig } from 'vite';

const base = process.env.BASE_URL || '/';

export default defineConfig({
  base,
});
