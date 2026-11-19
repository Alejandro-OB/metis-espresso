import { useMutation, useQueries, useQueryClient } from '@tanstack/react-query'
import { Link, useNavigate, useParams } from 'react-router-dom'
import { api, post, put } from '@/shared/api/client'
import type { Coffee, Page, PreparationProfile, ProfileSpecification, SensoryTarget } from '@/shared/api/types'
import { formatNumber } from '@/shared/lib/format'
import { Alert, Breadcrumbs, Button, LinkButton, LoadingState, Surface, useToast } from '@/shared/ui'
import { MoreActions } from './more-actions'
import { PageHeading } from './page-heading'
import { ProfileForm, type ProfilePayload } from './profile-form'
import { profilePropertyLabel } from './profile-options'
import type { SensoryTargetPayload } from './sensory-draft'
import { SustainmentExclusionControl } from './sustainment'
import { RecipeToleranceControl } from './recipe-tolerance'
import { sensoryDescriptorLabel } from './sensory-options'
import styles from './routes.module.css'

const statusLabels: Record<string, string> = { draft: 'Borrador', approved: 'Guardado', retired: 'Retirado', proposed: 'Propuesto', accepted: 'En uso en la máquina', effective: 'En uso en la máquina' }

export default function ProfileDetailRoute() {
  const { id = '' } = useParams()
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const toast = useToast()
  const [profile, coffees] = useQueries({ queries: [
    { queryKey: ['profile', id], queryFn: () => api<PreparationProfile>(`/api/v1/profiles/${id}`) },
    { queryKey: ['coffees', 'profile-options'], queryFn: () => api<Page<Coffee>>('/api/v1/coffees?page=1&page_size=100') },
  ] })
  const refresh = async () => { await Promise.all([queryClient.invalidateQueries({ queryKey: ['profile', id] }), queryClient.invalidateQueries({ queryKey: ['profiles'] })]) }
  // El sabor buscado va en su propia ruta del servidor; se guarda junto con
  // los valores, en el mismo botón. Sin nada marcado no se envía.
  const saveDraft = async ({ payload, target }: { payload: ProfilePayload; target: SensoryTargetPayload | null }) => {
    await put<PreparationProfile>(`/api/v1/profiles/${id}`, payload)
    if (target) await put<PreparationProfile>(`/api/v1/profiles/${id}/sensory-target`, target)
  }
  const save = useMutation({ mutationFn: saveDraft, onSuccess: async () => { await refresh(); toast.push('Cambios guardados', 'El borrador quedó actualizado.', 'success') } })
  // Guardar, aprobar y poner en uso de una vez: los tres pasos que antes eran
  // botones aparte. Es el mismo camino que «Usar en la máquina» de Preparar.
  const saveAndUse = useMutation({ mutationFn: async (draft: { payload: ProfilePayload; target: SensoryTargetPayload | null }) => { await saveDraft(draft); await post(`/api/v1/profiles/${id}/approve`, {}); return post<PreparationProfile>(`/api/v1/profiles/${id}/activate`, { via: 'edition' }) }, onSuccess: async () => { await refresh(); toast.push('Versión en uso', 'Esta versión rige ahora en la máquina.', 'success') } })
  const retire = useMutation({ mutationFn: () => post<PreparationProfile>(`/api/v1/profiles/${id}/retire`, {}), onSuccess: async () => { await refresh(); toast.push('Perfil retirado', 'La versión permanece disponible en el historial.', 'success') } })
  const version = useMutation({ mutationFn: (payload: ProfilePayload) => post<PreparationProfile>(`/api/v1/profiles/${id}/versions`, payload), onSuccess: (created) => { void queryClient.invalidateQueries({ queryKey: ['profiles'] }); void navigate(`/profiles/${created.id}`); toast.push('Nueva versión creada', 'Puedes ajustar el nuevo borrador.', 'success') } })
  const activate = useMutation({ mutationFn: () => post<PreparationProfile>(`/api/v1/profiles/${id}/activate`, { via: 'selection' }), onSuccess: async () => { await refresh(); toast.push('Versión en uso', 'Esta versión rige ahora en la máquina.', 'success') } })
  if (profile.isLoading || coffees.isLoading) return <LoadingState />
  if (profile.error || !profile.data) return <Alert variant="danger" title="No se encontró el perfil">{profile.error?.message ?? 'Sin datos'}</Alert>
  const item = profile.data
  const coffeeMap = new Map((coffees.data?.items ?? []).map((coffee) => [coffee.id, coffee]))
  const coffeeIds = item.coffeeIds ?? []
  const specifications = item.specifications ?? []
  const completeVersion = coffeeIds.length > 0 && specifications.length > 0
  const mutable = item.status === 'draft' && completeVersion
  const canRetire = ['approved', 'accepted', 'effective'].includes(item.status)
  const canVersion = completeVersion && (canRetire || item.status === 'retired')
  // La nueva versión hereda el purgado; sin él, el servidor pondría los 10 s
  // por omisión y un corte elegido se perdería sin que nadie lo decidiera.
  const payload = { name: item.name, coffeeIds, specifications, ...(item.purgeMaximumTimeMs != null ? { purgeMaximumTimeMs: item.purgeMaximumTimeMs } : {}) }
  const applicableCoffees = coffeeIds.flatMap((coffeeId) => coffeeMap.get(coffeeId) ?? [])
  const targetCoffee = coffeeMap.get(item.sensoryTarget?.coffeeId ?? coffeeIds[0] ?? '')
  return <div className={styles.page}>
    <Breadcrumbs items={[{ label: 'Perfiles', to: '/profiles' }, { label: `${item.name} · v${item.version}` }]}/>
    <div className={styles.profileHeader}>
      <PageHeading title={mutable && item.version > 1 ? `Versión nueva de ${item.name}` : item.name} detail={[`Versión ${item.version}`, statusLabels[item.status] ?? item.status, applicableCoffees.length ? `Café ${applicableCoffees.map((coffee) => coffee.commercialReference || coffee.lotIdentifier || coffee.bagIdentifier || coffee.id).join(', ')}` : null].filter(Boolean).join(' · ')} />
      <div className={styles.profileHeaderActions}>
        {item.status === 'approved' && completeVersion ? <Button onPress={() => activate.mutate()} isDisabled={activate.isPending}>Usar en la máquina</Button> : null}
        {canVersion ? <Button onPress={() => version.mutate(payload)} isDisabled={version.isPending}>Crear versión nueva</Button> : null}
        <MoreActions>
          {canRetire ? <Button variant="quiet" onPress={() => retire.mutate()} isDisabled={retire.isPending}>Retirar perfil</Button> : null}
          <LinkButton to={`/traceability/profile/${item.id}`} variant="quiet">Ver trazabilidad</LinkButton>
        </MoreActions>
      </div>
    </div>
    {save.error || saveAndUse.error || retire.error || version.error || activate.error ? <Alert variant="danger" title="No fue posible completar la acción">{(save.error || saveAndUse.error || retire.error || version.error || activate.error)?.message}</Alert> : null}
    {mutable ? <Surface>{targetCoffee?.declaredSensoryProfile ? <p className={styles.formHint}>El tostador describe este café así: “{targetCoffee.declaredSensoryProfile.originalText}”.</p> : null}<ProfileForm key={item.id} coffees={coffees.data?.items ?? []} initial={item} withSensory pending={save.isPending || saveAndUse.isPending} submitLabel="Guardar borrador" onSubmit={(changed, target) => save.mutate({ payload: changed, target })} onSubmitAndUse={(changed, target) => saveAndUse.mutate({ payload: changed, target })}/></Surface> : <div className={styles.stack}>
      <Surface><h2 className={styles.surfaceTitle}>Valores del perfil</h2>{specifications.length ? <div className={styles.profileValues}>{specifications.map((specification) => <div key={specification.property}><span>{profilePropertyLabel(specification.property)}</span><strong>{specificationText(specification)}</strong></div>)}</div> : <p className={styles.muted}>Esta propuesta usa el formato anterior.</p>}</Surface>
      <Surface><SensoryReferences target={item.sensoryTarget} coffee={targetCoffee}/></Surface>
      {canRetire ? <details className={styles.profileAdvanced}><summary>Ajustes avanzados</summary><div className={styles.stack}><RecipeToleranceControl profileId={item.id}/><SustainmentExclusionControl profileId={item.id}/></div></details> : null}
    </div>}
    {item.previousVersionId ? <p className={styles.meta}>Parte de <Link to={`/profiles/${item.previousVersionId}`}>la versión anterior</Link>.</p> : null}
  </div>
}

function specificationText(specification: ProfileSpecification): string {
  const number = (value: number | null | undefined) => formatNumber(value, Number.isInteger(value) ? 0 : 1)
  // La relación se escribe «1 : 2», como en Preparar café; con la unidad
  // pegada salía «2 :1».
  if (specification.unit === ':1') return specification.strategy === 'target' ? `1 : ${number(specification.target)}` : `1 : ${number(specification.minimum)}–${number(specification.maximum)}`
  if (specification.strategy === 'target') return `${number(specification.target)}${specification.tolerance == null ? '' : ` ± ${number(specification.tolerance)}`} ${specification.unit}`
  return `${number(specification.minimum)}–${number(specification.maximum)} ${specification.unit}`
}

// El sabor que busca el barista es lo que guía; lo que declara el tostador
// queda plegado, como referencia.
function SensoryReferences({ target, coffee }: { target: SensoryTarget | null | undefined; coffee: Coffee | undefined }) {
  const declared = coffee?.declaredSensoryProfile
  return <>
    <h2 className={styles.surfaceTitle}>Sabor buscado</h2>
    {target ? <><p>{target.rationale}</p><ul className={styles.list}>{target.descriptors.map((descriptor) => <li key={descriptor}>{sensoryDescriptorLabel(descriptor)}</li>)}{(target.otherDescriptors ?? []).map((descriptor) => <li key={`otro-${descriptor}`}>{descriptor}</li>)}</ul></> : <p className={styles.muted}>El barista no ha definido el sabor que busca.</p>}
    {declared ? <details className={styles.profileAdvanced}><summary>Ver la descripción del tostador</summary><p>{declared.originalText}</p><p className={styles.meta}>{declared.declaredBy} · {declared.source}</p><ul className={styles.list}>{declared.descriptors.map((descriptor) => <li key={descriptor}>{sensoryDescriptorLabel(descriptor)}</li>)}</ul></details> : null}
  </>
}
