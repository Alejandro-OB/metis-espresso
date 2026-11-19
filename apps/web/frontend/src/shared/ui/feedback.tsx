import { createContext, useCallback, useContext, useMemo, useState, type ReactNode } from 'react'
import { Icon, Surface } from './primitives'
import styles from './ui.module.css'

export type AlertVariant = 'info' | 'success' | 'warning' | 'danger'

export function Alert({ variant = 'info', title, children }: { variant?: AlertVariant; title: string; children: ReactNode }) {
  const icons = { info: 'info', success: 'check', warning: 'warning', danger: 'warning' } as const
  return <div className={`${styles.alert} ${styles[`alert${variant[0]?.toUpperCase()}${variant.slice(1)}`]}`} role={variant === 'danger' ? 'alert' : 'status'}>
    <Icon name={icons[variant]} />
    <div><p className={styles.alertTitle}>{title}</p><div className={styles.alertBody}>{children}</div></div>
  </div>
}

export function Progress({ value, label }: { value: number; label: string }) {
  const normalized = Math.min(100, Math.max(0, value))
  return <div aria-label={label} role="progressbar" aria-valuenow={normalized} aria-valuemin={0} aria-valuemax={100} className={styles.progress}><div className={styles.progressValue} style={{ transform: `scaleX(${normalized / 100})` }} /></div>
}

export function Skeleton({ height = '1rem' }: { height?: string }) { return <span className={styles.skeleton} style={{ minHeight: height }} aria-hidden="true" /> }

export function EmptyState({ title, children }: { title: string; children: ReactNode }) {
  return <div className={styles.empty}><div className={styles.emptyMark}>∅</div><h3>{title}</h3><p>{children}</p></div>
}

interface ToastItem { id: number; title: string; message: string; variant: AlertVariant }
interface ToastContextValue { push: (title: string, message: string, variant?: AlertVariant) => void }
const ToastContext = createContext<ToastContextValue | null>(null)

export function ToastProvider({ children }: { children: ReactNode }) {
  const [items, setItems] = useState<ToastItem[]>([])
  const push = useCallback((title: string, message: string, variant: AlertVariant = 'info') => {
    const id = Date.now()
    setItems((current) => [...current, { id, title, message, variant }])
    window.setTimeout(() => setItems((current) => current.filter((item) => item.id !== id)), 5000)
  }, [])
  const value = useMemo(() => ({ push }), [push])
  return <ToastContext.Provider value={value}>{children}<div className={styles.toastRegion} aria-live="polite" aria-label="Notificaciones">{items.map((item) => <div className={styles.toast} key={item.id}><strong>{item.title}</strong><div>{item.message}</div></div>)}</div></ToastContext.Provider>
}

// eslint-disable-next-line react-refresh/only-export-components
export function useToast() {
  const value = useContext(ToastContext)
  if (!value) throw new Error('useToast debe usarse dentro de ToastProvider')
  return value
}

export function LoadingState({ label = 'Cargando datos' }: { label?: string }) {
  return <Surface><div aria-label={label} role="status" style={{ display: 'grid', gap: '1rem' }}><Skeleton height="2rem"/><Skeleton/><Skeleton/><Skeleton height="8rem"/></div></Surface>
}

export function ErrorState({ error }: { error: Error }) {
  return <Surface><Alert variant="danger" title="No pudimos cargar esta vista"><p>{error.message}</p><p>Comprueba la conexión y vuelve a intentarlo.</p></Alert></Surface>
}
