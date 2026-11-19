import { render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { DataTable, type Column } from './data-table'

interface Row { id: string; value: number }
const columns: Column<Row>[] = [{ id: 'id', header: 'ID', cell: (row) => row.id }, { id: 'value', header: 'Valor', cell: (row) => row.value, numeric: true }]

describe('DataTable', () => {
  it('expone tabla y alternativa de registros con el mismo nombre', () => {
    render(<DataTable label="Resultados" rows={[{ id: 'a', value: 10 }]} columns={columns} getKey={(row) => row.id}/>)
    expect(screen.getByRole('table', { name: 'Resultados' })).toBeInTheDocument()
    expect(screen.getAllByText('a').length).toBeGreaterThan(0)
  })
  it('comunica el estado vacío', () => {
    render(<DataTable label="Resultados" rows={[]} columns={columns} getKey={(row) => row.id}/>)
    expect(screen.getByText('No hay registros')).toBeInTheDocument()
  })
})
