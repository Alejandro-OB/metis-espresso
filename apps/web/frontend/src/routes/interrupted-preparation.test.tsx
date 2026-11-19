import { render, screen } from '@testing-library/react'
import { describe, expect, it } from 'vitest'
import { InterruptedPerformance, InterruptedPreparationNotice } from './interrupted-preparation'
import { interruptedByPowerLoss, preparationMomentLabel } from './preparation-status'

/* Una preparación cortada por falta de energía llega con el final, la duración
   y a veces el inicio en nulo. Lo que se fija aquí es que la pantalla lo diga
   así —incompleta, duración desconocida— y no muestre un cero, una fecha
   rellenada ni indicadores que nadie calculó. */
const cortada = { termination: 'unexpected-power-loss', startedAt: null, finishedAt: null, sampleCount: 0 }

describe('preparación cortada por falta de energía', () => {
  it('se reconoce por su terminación y solo por ella', () => {
    expect(interruptedByPowerLoss(cortada)).toBe(true)
    expect(interruptedByPowerLoss({ termination: 'manual-normal-stop' })).toBe(false)
  })

  it('no inventa una fecha cuando no consta ninguna', () => {
    expect(preparationMomentLabel(cortada)).toBe('Sin hora de inicio · incompleta')
    expect(preparationMomentLabel({ ...cortada, startedAt: '2026-10-12T09:00:00Z' })).toMatch(/^Empezó .+ · incompleta$/)
  })

  it('se anuncia como incompleta, con la duración desconocida', () => {
    render(<InterruptedPreparationNotice extraction={cortada}/>)
    expect(screen.getByText('Preparación incompleta')).toBeInTheDocument()
    expect(screen.getByText(/Interrumpida por corte de energía; duración desconocida/)).toBeInTheDocument()
    expect(screen.getByText(/el inicio tampoco consta/)).toBeInTheDocument()
  })

  it('muestra la duración como desconocida y ningún indicador', () => {
    render(<InterruptedPerformance extraction={cortada}/>)
    expect(screen.getByText('Desconocida')).toBeInTheDocument()
    expect(screen.getByText('Pérdida de energía')).toBeInTheDocument()
    expect(screen.queryByText(/Error medio/)).not.toBeInTheDocument()
  })
})
