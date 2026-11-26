import { useState, type Key } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { useNavigate } from 'react-router-dom'
import { api, post } from '@/shared/api/client'
import type { Extraction, Page, PreparationProfile } from '@/shared/api/types'
import { formatDateTime, formatNumber } from '@/shared/lib/format'
import { Alert, Button, DataTable, Surface, useToast, type Column } from '@/shared/ui'
import { profilePropertyLabel } from './profile-options'
import styles from './routes.module.css'

/**
 * La tolerancia de la receta, calculada desde los cafés que el barista repitió
 * con ella (protocolo de catación, paso 2). El cálculo es del servidor
 * (`espresso_profiles.dispersion`): aquí solo se eligen los cafés, se muestra lo
 * propuesto y se confirma. Confirmar crea la versión, la aprueba y la pone a
 * regir, igual que «Guardar y usar en la máquina».
 *
 * Solo se ofrecen cafés medidos con este mismo perfil; el servidor lo vuelve a
 * comprobar. No toca un intervalo, como el del tiempo, cuyo máximo es el corte:
 * de él solo se muestra cuánto variaron los cafés.
 */

type Detail = { property: string; unit: string; values: Array<number | null>; status: 'updated' | 'added' | 'range' | 'missing'; objective?: number; tolerance?: number; mean?: number; spread?: number }
type Proposal = { details: Detail[]; profile?: PreparationProfile }

function describe(detail: Detail): string {
  if (detail.status === 'missing') return 'Falta el valor en algún café: no se cambia.'
  if (detail.status === 'range') return `Intervalo: no se cambia. Media ${formatNumber(detail.mean, 2)} ± ${formatNumber(detail.spread, 2)} ${detail.unit}.`
  return `${formatNumber(detail.objective, 2)} ± ${formatNumber(detail.tolerance, 2)} ${detail.unit}${detail.status === 'added' ? ' (objetivo: la media)' : ''}`
}

export function RecipeToleranceControl({ profileId }: { profileId: string }) {
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const toast = useToast()
  const [chosen, setChosen] = useState<Set<Key>>(new Set())
  const cups = useQuery({ queryKey: ['extractions', 'recipe-tolerance', profileId], queryFn: () => api<Page<Extraction>>(`/api/v1/extractions?page=1&page_size=20&source=measured&profileId=${encodeURIComponent(profileId)}`) })
  const ids = [...chosen].map(String)
  const proposal = useMutation({ mutationFn: () => post<Proposal>(`/api/v1/profiles/${profileId}/tolerance-from-extractions`, { extractionIds: ids, apply: false }) })
  const apply = useMutation({ mutationFn: () => post<Proposal>(`/api/v1/profiles/${profileId}/tolerance-from-extractions`, { extractionIds: ids, apply: true }), onSuccess: (result) => { void queryClient.invalidateQueries({ queryKey: ['profiles'] }); toast.push('Tolerancia fijada', 'La versión nueva rige en la máquina.', 'success'); if (result.profile) void navigate(`/profiles/${result.profile.id}`) } })
  const choose = (next: Set<Key>) => { proposal.reset(); setChosen(next) }
  const items = cups.data?.items ?? []
  // Una tabla con casillas en vez de una línea larga con puntos, y sin frase de
  // instrucciones (23-oct-2026): el título y el botón dicen qué hacer.
  const columns: Column<Extraction>[] = [
    { id: 'date', header: 'Fecha', cell: (cup) => formatDateTime(cup.finishedAt) },
    { id: 'dose', header: 'Dosis', numeric: true, cell: (cup) => `${formatNumber(cup.manualMeasurements?.coffeeDoseG)} g` },
    { id: 'cup', header: 'Taza', numeric: true, cell: (cup) => `${formatNumber(cup.manualMeasurements?.beverageMassG)} g` },
    { id: 'time', header: 'Tiempo', numeric: true, cell: (cup) => cup.durationMs == null ? '—' : `${formatNumber(cup.durationMs / 1000)} s` },
    { id: 'tds', header: 'TDS', numeric: true, cell: (cup) => `${formatNumber(cup.manualMeasurements?.tdsPercent, 2)} %` },
  ]
  return <Surface>
    <h2 className={styles.surfaceTitle}>Repeticiones de este perfil</h2>
    {cups.error ? <Alert variant="danger" title="No fue posible cargar los cafés">{cups.error.message}</Alert> : null}
    {items.length || cups.isLoading ? <DataTable label="Cafés hechos con este perfil" rows={items} columns={columns} getKey={(cup) => cup.id} loading={cups.isLoading} selectedKeys={chosen} onSelectionChange={choose}/> : <p className={styles.muted}>Aún no hay cafés con este perfil.</p>}
    {items.length ? <div className={styles.recipeActions}><Button variant="secondary" isDisabled={ids.length < 2 || proposal.isPending} onPress={() => proposal.mutate()}>Calcular tolerancia</Button>{proposal.data?.details.some((detail) => detail.status === 'updated' || detail.status === 'added') ? <Button isDisabled={apply.isPending} onPress={() => apply.mutate()}>Usar en la máquina</Button> : null}</div> : null}
    {proposal.data ? <div className={styles.specificationSummary}>{proposal.data.details.map((detail) => <div key={detail.property}><strong>{profilePropertyLabel(detail.property)}</strong><span>{describe(detail)}</span></div>)}</div> : null}
    {proposal.error || apply.error ? <Alert variant="danger" title="No fue posible calcular la tolerancia">{(proposal.error || apply.error)?.message}</Alert> : null}
  </Surface>
}
