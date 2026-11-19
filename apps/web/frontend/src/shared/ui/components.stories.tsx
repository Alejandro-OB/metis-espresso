import type { Meta, StoryObj } from '@storybook/react-vite'
import { Alert, Button, EmptyState, Measurement, ProvenanceBadge, ServiceStatus, Surface } from '.'

const meta = { title: 'Sistema/Inventario esencial', component: Surface } satisfies Meta<typeof Surface>
export default meta
type Story = StoryObj<typeof meta>

export const Estados: Story = { args: { children: null }, render: () => <Surface><div style={{ display: 'grid', gap: '1rem' }}><Alert title="Información trazable">Cada valor conserva origen y versión.</Alert><Alert variant="warning" title="Servicio degradado">La referencia técnica se abstiene.</Alert><ServiceStatus state="available">Disponible</ServiceStatus><ProvenanceBadge source="measured"/><ProvenanceBadge source="simulation"/><Measurement label="Temperatura" value="93,0" unit="°C"/><Button icon="arrow">Continuar</Button></div></Surface> }
export const Vacio: Story = { args: { children: null }, render: () => <Surface><EmptyState title="Sin registros">El estado vacío comunica qué dato falta.</EmptyState></Surface> }
