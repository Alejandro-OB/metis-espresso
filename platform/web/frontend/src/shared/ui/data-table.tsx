import { useMemo, useState, type Key, type ReactNode } from 'react'
import { Button, Checkbox, Field, SearchField, Select } from './primitives'
import { EmptyState, Skeleton } from './feedback'
import styles from './ui.module.css'

export interface Column<T> {
  id: string
  header: string
  cell: (item: T) => ReactNode
  mobile?: boolean
  numeric?: boolean
  sortable?: boolean
  sortValue?: (item: T) => string | number
}

interface DataTableProps<T> {
  label: string
  rows: T[]
  columns: Column<T>[]
  getKey: (item: T) => Key
  loading?: boolean
  error?: Error | null
  emptyTitle?: string
  emptyMessage?: string
  selectedKeys?: Set<Key>
  onSelectionChange?: (keys: Set<Key>) => void
}

export function DataTable<T>({ label, rows, columns, getKey, loading = false, error, emptyTitle = 'No hay registros', emptyMessage = 'Los datos aparecerán aquí cuando estén disponibles.', selectedKeys, onSelectionChange }: DataTableProps<T>) {
  const [sort, setSort] = useState<{ id: string; direction: 'asc' | 'desc' } | null>(null)
  const visible = columns
  const ordered = useMemo(() => {
    if (!sort) return rows
    const column = columns.find((item) => item.id === sort.id)
    if (!column?.sortValue) return rows
    return [...rows].sort((left, right) => {
      const a = column.sortValue?.(left) ?? ''
      const b = column.sortValue?.(right) ?? ''
      return (a < b ? -1 : a > b ? 1 : 0) * (sort.direction === 'asc' ? 1 : -1)
    })
  }, [columns, rows, sort])
  if (loading) return <div aria-label={`Cargando ${label}`}><Skeleton height="16rem" /></div>
  if (error) return <div role="alert">{error.message}</div>
  if (!rows.length) return <EmptyState title={emptyTitle}>{emptyMessage}</EmptyState>
  const toggle = (key: Key) => {
    if (!selectedKeys || !onSelectionChange) return
    const next = new Set(selectedKeys)
    if (next.has(key)) next.delete(key); else next.add(key)
    onSelectionChange(next)
  }
  const updateSort = (column: Column<T>) => {
    if (!column.sortable) return
    setSort((current) => current?.id === column.id ? { id: column.id, direction: current.direction === 'asc' ? 'desc' : 'asc' } : { id: column.id, direction: 'asc' })
  }
  return <>
    <div className={styles.tableWrap}>
      <table className={styles.table}>
        <caption>{label}</caption>
        <thead><tr>{selectedKeys ? <th scope="col"><span className="sr-only">Seleccionar</span></th> : null}{visible.map((column) => {
          const activeSort = sort?.id === column.id
          return <th scope="col" aria-sort={activeSort ? (sort.direction === 'asc' ? 'ascending' : 'descending') : undefined} className={column.numeric ? styles.numeric : ''} key={column.id}>
            {column.sortable
              ? <button className={styles.sortButton} onClick={() => updateSort(column)}>{column.header}{activeSort ? <span aria-hidden="true">{sort.direction === 'asc' ? '↑' : '↓'}</span> : null}</button>
              : <span className={styles.headerLabel}>{column.header}</span>}
          </th>
        })}</tr></thead>
        <tbody>{ordered.map((row) => { const key = getKey(row); return <tr key={key}>{selectedKeys ? <td><Checkbox aria-label={`Seleccionar ${String(key)}`} label="" checked={selectedKeys.has(key)} onChange={() => toggle(key)} /></td> : null}{visible.map((column) => <td key={column.id} className={column.numeric ? styles.numeric : ''}>{column.cell(row)}</td>)}</tr> })}</tbody>
      </table>
    </div>
    <div className={styles.cards} aria-label={label}>{ordered.map((row) => { const key = getKey(row); return <article className={styles.recordCard} key={key}>{selectedKeys ? <Checkbox label="Comparar este registro" checked={selectedKeys.has(key)} onChange={() => toggle(key)} /> : null}{visible.filter((column) => column.mobile !== false).map((column) => <div className={styles.recordRow} key={column.id}><span className={styles.recordLabel}>{column.header}</span><div className={column.numeric ? styles.numeric : ''}>{column.cell(row)}</div></div>)}</article> })}</div>
  </>
}

export function TableToolbar({ query, onQueryChange, children }: { query: string; onQueryChange: (query: string) => void; children?: ReactNode }) {
  return <div className={styles.toolbar}><Field label="Buscar" description="Identificador, café, lote, origen o barista"><SearchField value={query} onChange={(event) => onQueryChange(event.target.value)} placeholder="Buscar en el historial…" /></Field><div className={styles.buttonGroup}>{children}</div></div>
}

// Dos flechas con el número de página en medio, a la derecha. Los botones de
// texto en píldora pesaban más que la tabla que acompañan. Con una sola
// página no hay a dónde ir y no se muestra nada.
export function Pagination({ page, pages, onChange }: { page: number; pages: number; onChange: (page: number) => void }) {
  if (pages <= 1) return null
  return <nav className={styles.pagination} aria-label="Paginación">
    <Button variant="quiet" className={styles.pageArrow} aria-label="Página anterior" isDisabled={page <= 1} onPress={() => onChange(page - 1)}>‹</Button>
    <span>Página {page.toLocaleString('es-CO')} de {pages.toLocaleString('es-CO')}</span>
    <Button variant="quiet" className={styles.pageArrow} aria-label="Página siguiente" isDisabled={page >= pages} onPress={() => onChange(page + 1)}>›</Button>
  </nav>
}

export function ColumnVisibility({ columns, visible, onChange }: { columns: Array<{ id: string; label: string }>; visible: Set<string>; onChange: (value: Set<string>) => void }) {
  return <fieldset className={styles.field}><legend className={styles.label}>Columnas visibles</legend>{columns.map((column) => <Checkbox key={column.id} label={column.label} checked={visible.has(column.id)} onChange={() => { const next = new Set(visible); if (next.has(column.id)) next.delete(column.id); else next.add(column.id); onChange(next) }} />)}</fieldset>
}

export function SortControl({ value, options, onChange }: { value: string; options: Array<{ value: string; label: string }>; onChange: (value: string) => void }) {
  return <Field label="Orden"><Select value={value} onChange={(event) => onChange(event.target.value)}>{options.map((option) => <option value={option.value} key={option.value}>{option.label}</option>)}</Select></Field>
}

export function FilterPanel({ children }: { children: ReactNode }) { return <div className={styles.surfaceShell}><div className={styles.surface}>{children}</div></div> }
