import { useEffect } from 'react'
import { useQueryClient } from '@tanstack/react-query'
import { useToast } from '@/shared/ui'
import { flushPendingWrites } from '@/shared/api/client'

export function PwaLifecycle() {
  const toast = useToast()
  const queryClient = useQueryClient()
  useEffect(() => {
    if (!('serviceWorker' in navigator) || import.meta.env.DEV) return
    let registration: ServiceWorkerRegistration | undefined
    void navigator.serviceWorker.register('/sw.js').then((value) => {
      registration = value
      value.addEventListener('updatefound', () => {
        const worker = value.installing
        worker?.addEventListener('statechange', () => {
          if (worker.state === 'installed' && navigator.serviceWorker.controller) toast.push('Actualización disponible', 'Recarga cuando termines la tarea actual para aplicar la nueva versión.', 'info')
        })
      })
    })
    return () => registration?.installing?.removeEventListener('statechange', () => undefined)
  }, [toast])
  useEffect(() => {
    const flush = async () => {
      const sent = await flushPendingWrites()
      if (!sent) return
      await queryClient.invalidateQueries()
      toast.push('Acciones enviadas', `${sent} ${sent === 1 ? 'acción pendiente fue guardada' : 'acciones pendientes fueron guardadas'}.`, 'success')
    }
    const onOnline = () => { void flush() }
    window.addEventListener('online', onOnline)
    if (navigator.onLine) void flush()
    return () => window.removeEventListener('online', onOnline)
  }, [queryClient, toast])
  return null
}
