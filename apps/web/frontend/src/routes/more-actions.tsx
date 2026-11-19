import { useState, type ReactNode } from 'react'
import { Button } from '@/shared/ui'
import styles from './routes.module.css'

// Lo que se usa poco (retirar, trazabilidad, descargas) va en un menú corto
// para que arriba queden solo las acciones principales.
export function MoreActions({ children }: { children: ReactNode }) {
  const [open, setOpen] = useState(false)
  return <div className={styles.moreActions}>
    <Button variant="secondary" aria-expanded={open} onPress={() => setOpen((current) => !current)}>Más</Button>
    {open ? <div className={styles.moreActionsMenu} onClick={() => setOpen(false)}>{children}</div> : null}
  </div>
}

