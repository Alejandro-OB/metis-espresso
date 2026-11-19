import { defineConfig } from 'vitest/config'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  resolve: { alias: { '@': new URL('./src', import.meta.url).pathname } },
  server: {
    host: '0.0.0.0',
    // Un montaje de Docker sobre Windows o macOS no propaga los avisos de
    // cambio del sistema de archivos al contenedor: Vite no se entera de que
    // un archivo se editó y sigue sirviendo el módulo anterior aunque se
    // recargue la página. El sondeo lo activa compose.dev.yaml, que es donde
    // existe ese montaje; ejecutando `npm run dev` directamente en la máquina
    // los avisos funcionan y sondear solo gastaría procesador.
    watch: process.env.VITE_WATCH_POLLING ? { usePolling: true, interval: 300 } : undefined,
    proxy: { '/api': process.env.VITE_API_PROXY_TARGET ?? 'http://localhost:8000', '/events': process.env.VITE_API_PROXY_TARGET ?? 'http://localhost:8000' },
  },
  build: { sourcemap: true, target: 'es2022' },
  test: { environment: 'jsdom', setupFiles: './src/test-setup.ts', exclude: ['e2e/**', 'node_modules/**'] },
})
