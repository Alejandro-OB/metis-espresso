import { QueryClient, QueryClientProvider } from '@tanstack/react-query'
import { AppRouter } from './router'
import { PwaLifecycle } from './pwa'
import { ToastProvider } from '@/shared/ui'
import { AuthProvider } from './auth'
import { useAuth } from './auth-context'
import Login from '@/routes/login'

const queryClient = new QueryClient({
  defaultOptions: {
    queries: { staleTime: 30_000, retry: 1, refetchOnWindowFocus: false },
    mutations: { retry: 0 },
  },
})

export function App() {
  return <QueryClientProvider client={queryClient}><AuthProvider><ToastProvider><PwaLifecycle/><SessionGate/></ToastProvider></AuthProvider></QueryClientProvider>
}

function SessionGate() {
  const { user, isLoading } = useAuth()
  if (isLoading) return <div role="status" aria-live="polite" style={{ padding: '2rem' }}>Cargando…</div>
  return user ? <AppRouter/> : <Login/>
}
