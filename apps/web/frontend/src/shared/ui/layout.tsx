import { useEffect, useRef, useState, type KeyboardEvent, type ReactNode } from 'react'
import { Link, NavLink, useLocation } from 'react-router-dom'
import type { SessionUser } from '@/shared/api/types'
import { Button, Icon, type IconName } from './primitives'
import { pendingWriteCount, pendingWritesEvent } from '@/shared/api/client'
import styles from './layout.module.css'

/* El menú de la catación con el barista (pedido del usuario del 22-oct-2026):
   solo lo que el protocolo usa. Los demás módulos siguen existiendo y se llega
   a ellos por su dirección, pero no se ofrecen mientras se mejora su
   presentación; al volver a ofrecerlos, se agregan otra vez a `navigation`.
   «Resumen» deja de ser la portada: se entra a «Preparar café». */
const navigation: Array<{ to: string; label: string; icon: IconName }> = [
  { to: '/prepare', label: 'Preparar café', icon: 'coffee' },
  { to: '/extractions', label: 'Historial', icon: 'history' },
  { to: '/profiles', label: 'Perfiles', icon: 'system' },
  { to: '/episodes', label: 'Ajustes', icon: 'history' },
  { to: '/recommendations', label: 'Recomendaciones', icon: 'evidence' },
  { to: '/coffees', label: 'Cafés', icon: 'coffee' },
]

// Ocultos durante la catación, con su dirección para volver a ofrecerlos:
// Resumen /summary · Práctica /extract · Guía /guide · Gemelo virtual
// /simulations · Equipos /equipment · Puesta a punto /calibrations · Agua
// /water-references.

type NavItem = { to: string; label: string; icon: IconName }

function conAdministracion(admin: boolean): NavItem[] {
  return admin ? [...navigation, { to: '/system', label: 'Administración', icon: 'system' }] : navigation
}

function enlace(item: NavItem, onSelect?: () => void) {
  return <NavLink end={item.to === '/'} key={item.to} to={item.to} onClick={onSelect} className={({ isActive }) => `${styles.navLink} ${isActive ? styles.active : ''}`}><Icon name={item.icon}/><span>{item.label}</span></NavLink>
}

export function SideNavigation({ admin = false }: { admin?: boolean }) {
  return <nav className={styles.nav} aria-label="Navegación principal">{conAdministracion(admin).map((item) => enlace(item))}</nav>
}

/* Cuántos destinos quedan a la vista en la isla inferior. Once no caben en el
   ancho de un teléfono: se repartían en dos filas de iconos sin etiqueta que
   ocupaban un cuarto de la pantalla, y cuatro pares de esos iconos son el
   mismo dibujo. A la vista quedan los del recorrido diario —ver el estado,
   extraer, consultar el historial, elegir el café— y el resto, que es
   configuración y análisis, se despliega desde «Más».

   No se omite ningún destino: la barra lateral está oculta en móvil y cinco
   secciones no reciben enlaces desde ninguna página, así que esta isla es su
   única entrada. */
const DESTINOS_A_LA_VISTA = 4

export function BottomNavigation({ admin = false }: { admin?: boolean }) {
  const items = conAdministracion(admin)
  const visibles = items.slice(0, DESTINOS_A_LA_VISTA)
  const ocultos = items.slice(DESTINOS_A_LA_VISTA)
  const [desplegado, setDesplegado] = useState(false)
  const zona = useRef<HTMLDivElement>(null)
  const { pathname } = useLocation()

  // El destino actual puede estar entre los ocultos. Sin esta marca, quien
  // está en Equipos no tiene en toda la pantalla ninguna señal de dónde está.
  const actualOculto = ocultos.some((item) => pathname === item.to || pathname.startsWith(`${item.to}/`))

  useEffect(() => { setDesplegado(false) }, [pathname])

  useEffect(() => {
    if (!desplegado) return
    const fuera = (event: PointerEvent) => { if (!zona.current?.contains(event.target as Node)) setDesplegado(false) }
    const escape = (event: globalThis.KeyboardEvent) => { if (event.key === 'Escape') setDesplegado(false) }
    document.addEventListener('pointerdown', fuera)
    document.addEventListener('keydown', escape)
    return () => { document.removeEventListener('pointerdown', fuera); document.removeEventListener('keydown', escape) }
  }, [desplegado])

  return <div className={styles.dock} ref={zona}>
    {desplegado ? <nav className={styles.morePanel} aria-label="Más secciones">{ocultos.map((item) => enlace(item, () => setDesplegado(false)))}</nav> : null}
    <nav className={styles.bottom} aria-label="Navegación móvil">
      {visibles.map((item) => enlace(item))}
      <button type="button" className={`${styles.navLink} ${styles.moreButton} ${desplegado || actualOculto ? styles.active : ''}`} aria-expanded={desplegado} onClick={() => setDesplegado((valor) => !valor)}>
        <Icon name="more"/><span>Más</span>
      </button>
    </nav>
  </div>
}

function useOnline() {
  const [online, setOnline] = useState(() => navigator.onLine)
  useEffect(() => {
    const update = () => setOnline(navigator.onLine)
    window.addEventListener('online', update)
    window.addEventListener('offline', update)
    return () => { window.removeEventListener('online', update); window.removeEventListener('offline', update) }
  }, [])
  return online
}

function usePendingWriteCount() {
  const [count, setCount] = useState(pendingWriteCount)
  useEffect(() => {
    const update = () => setCount(pendingWriteCount())
    window.addEventListener(pendingWritesEvent, update)
    return () => window.removeEventListener(pendingWritesEvent, update)
  }, [])
  return count
}

// «Barista de laboratorio» → «BL»: dos letras bastan para reconocerse.
function initials(name: string) {
  return name.split(/\s+/).filter((word) => word.length > 2 || /^[A-ZÁÉÍÓÚÑ]/.test(word)).slice(0, 2).map((word) => word[0].toUpperCase()).join('') || name.slice(0, 1).toUpperCase()
}

export function AppShell({ children, user, onLogout }: { children: ReactNode; user: SessionUser; onLogout: () => Promise<void> }) {
  const online = useOnline()
  const pending = usePendingWriteCount()
  return <div className={styles.shell}>
    <aside className={styles.side}><div className={styles.sideInner}>
      <Link to="/prepare" className={styles.brand}><span className={styles.brandMark}><Icon name="coffee" size={22}/></span><span className={styles.brandText}><strong>Bitácora de espresso</strong></span></Link>
      <SideNavigation admin={user.role === 'admin'} />
    </div></aside>
    <main id="main-content" className={styles.main}>
      <header className={styles.top}>
        {/* La conexión solo se nombra cuando falta o hay algo por enviar
            (26-oct-2026): «Con conexión» siempre a la vista no decía de qué
            era y no pedía nada. */}
        {!online || pending ? <span className={`${styles.network} ${online ? '' : styles.offline}`}><span className={styles.networkDot}/>{online ? 'Enviando' : 'Sin conexión'}{pending ? ` · ${pending} ${pending === 1 ? 'cambio por enviar' : 'cambios por enviar'}` : ''}</span> : null}
        <div className={styles.account}>
          <Link className={styles.accountLink} to={`/traceability/barista/${user.id}`}><span className={styles.avatar} aria-hidden="true">{initials(user.displayName)}</span><span className={styles.accountText}><strong>{user.displayName}</strong><small>{user.role === 'admin' ? 'Administración' : 'Barista'}</small></span></Link>
          <Button variant="quiet" icon="logout" className={styles.logout} onPress={() => void onLogout()}>Cerrar sesión</Button>
        </div>
      </header>
      {children}
    </main>
    <BottomNavigation admin={user.role === 'admin'} />
  </div>
}

export function Breadcrumbs({ items }: { items: Array<{ label: string; to?: string }> }) {
  return <nav className={styles.breadcrumbs} aria-label="Ruta de navegación">{items.map((item, index) => <span key={`${item.label}-${index}`}>{index ? ' / ' : ''}{item.to ? <Link to={item.to}>{item.label}</Link> : item.label}</span>)}</nav>
}

export function Tabs({ tabs, active, onChange }: { tabs: Array<{ id: string; label: string }>; active: string; onChange: (id: string) => void }) {
  const refs = useRef<Array<HTMLButtonElement | null>>([])
  const move = (event: KeyboardEvent<HTMLButtonElement>, index: number) => {
    const key = event.key
    if (!['ArrowLeft', 'ArrowRight', 'Home', 'End'].includes(key)) return
    event.preventDefault()
    const next = key === 'Home' ? 0 : key === 'End' ? tabs.length - 1 : (index + (key === 'ArrowRight' ? 1 : -1) + tabs.length) % tabs.length
    refs.current[next]?.focus()
    onChange(tabs[next].id)
  }
  return <div className={styles.tabs} role="tablist" aria-label="Secciones">{tabs.map((tab, index) => <button ref={(node) => { refs.current[index] = node }} role="tab" tabIndex={active === tab.id ? 0 : -1} aria-selected={active === tab.id} className={`${styles.tab} ${active === tab.id ? styles.tabActive : ''}`} key={tab.id} onClick={() => onChange(tab.id)} onKeyDown={(event) => move(event, index)}>{tab.label}</button>)}</div>
}
