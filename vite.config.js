import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    port: 3000,
    // Redirige las llamadas a la API al backend de C++ en :8080
    proxy: {
      '/execute': 'http://localhost:8080',
      '/report':  'http://localhost:8080',
    },
  },
})
