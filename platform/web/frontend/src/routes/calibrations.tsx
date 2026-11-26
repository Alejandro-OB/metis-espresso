import { useEffect, useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { Link, useNavigate } from 'react-router-dom'
import { api, post } from '@/shared/api/client'
import type { Calibration, Coffee, DashboardResponse, Page, PreparationProfile } from '@/shared/api/types'
import { Alert, Button, DataTable, Field, Input, Modal, Pagination, Select, Surface, TextArea, type Column, useToast } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

function coffeeName(coffee: Coffee) {
  return coffee.commercialReference || coffee.lotIdentifier || coffee.bagIdentifier || coffee.id
}

export default function CalibrationsRoute() {
  const [page, setPage] = useState(1)
  const [open, setOpen] = useState(false)
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const toast = useToast()
  const calibrations = useQuery({ queryKey: ['calibrations', page], queryFn: () => api<Page<Calibration>>(`/api/v1/calibrations?page=${page}`) })
  const dashboard = useQuery({ queryKey: ['dashboard'], queryFn: () => api<DashboardResponse>('/api/v1/dashboard') })
  const coffees = useQuery({ queryKey: ['coffees', 'calibration-options'], queryFn: () => api<Page<Coffee>>('/api/v1/coffees?page=1&page_size=100') })
  const profiles = useQuery({ queryKey: ['profiles', 'calibration-options'], queryFn: () => api<Page<PreparationProfile>>('/api/v1/profiles?page=1') })
  const create = useMutation({ mutationFn: (payload: Record<string, unknown>) => post<Calibration>('/api/v1/calibrations', payload), onSuccess: async (item) => { localStorage.removeItem('espresso:calibration-draft'); await queryClient.invalidateQueries({ queryKey: ['calibrations'] }); toast.push('Puesta a punto iniciada', 'Ya puedes registrar las preparaciones de prueba.', 'success'); void navigate(`/calibrations/${item.id}`) } })
  const coffeeById = new Map((coffees.data?.items ?? []).map((coffee) => [coffee.id, coffeeName(coffee)]))
  const columns: Column<Calibration>[] = [
    { id: 'startedAt', header: 'Inicio', cell: (item) => <Link to={`/calibrations/${item.id}`}>{formatDateTime(item.startedAt)}</Link> },
    { id: 'coffee', header: 'Café', cell: (item) => coffeeById.get(item.coffeeId) ?? item.coffeeId },
    { id: 'profile', header: 'Perfil previo', cell: (item) => item.profileName ?? 'Sin perfil previo' },
    { id: 'tests', header: 'Pruebas', cell: (item) => item.testExtractionIds.length, numeric: true },
    { id: 'status', header: 'Estado', cell: (item) => item.status === 'open' ? 'En curso' : 'Finalizada' },
  ]
  return <div className={styles.page}>
    <PageHeading title="Puestas a punto"/>
    <Surface><div className={styles.listActions}><Button icon="plus" onPress={() => setOpen(true)}>Iniciar puesta a punto</Button></div>{calibrations.error ? <Alert variant="danger" title="No fue posible cargar las puestas a punto">{calibrations.error.message}</Alert> : <DataTable label="Puestas a punto" rows={calibrations.data?.items ?? []} columns={columns} getKey={(item) => item.id} loading={calibrations.isLoading}/>}<Pagination page={calibrations.data?.page ?? page} pages={calibrations.data?.pages ?? 1} onChange={setPage}/></Surface>
    <StartCalibrationModal isOpen={open} onOpenChange={setOpen} coffees={coffees.data?.items ?? []} profiles={profiles.data?.items ?? []} activeCoffeeId={dashboard.data?.activeCoffee?.id ?? ''} activeProfileId={dashboard.data?.activeProfile?.id ?? ''} pending={create.isPending} error={create.error} onSubmit={(payload) => create.mutate(payload)}/>
  </div>
}

const calibrationDraftKey = 'espresso:calibration-draft'
function readCalibrationDraft() {
  try { return JSON.parse(localStorage.getItem(calibrationDraftKey) ?? 'null') as { coffeeId: string; profileId: string; station: string; notes: string } | null }
  catch { return null }
}

function StartCalibrationModal({ isOpen, onOpenChange, coffees, profiles, activeCoffeeId, activeProfileId, pending, error, onSubmit }: { isOpen: boolean; onOpenChange: (open: boolean) => void; coffees: Coffee[]; profiles: PreparationProfile[]; activeCoffeeId: string; activeProfileId: string; pending: boolean; error: Error | null; onSubmit: (payload: Record<string, unknown>) => void }) {
  const [initialDraft] = useState(readCalibrationDraft)
  const [coffeeId, setCoffeeId] = useState(initialDraft?.coffeeId ?? '')
  const [profileId, setProfileId] = useState(initialDraft?.profileId ?? '')
  const [station, setStation] = useState(initialDraft?.station ?? '')
  const [notes, setNotes] = useState(initialDraft?.notes ?? '')
  useEffect(() => {
    if (initialDraft || !activeCoffeeId) return
    setCoffeeId(activeCoffeeId)
    setProfileId(activeProfileId)
  }, [activeCoffeeId, activeProfileId, initialDraft])
  useEffect(() => {
    try { localStorage.setItem(calibrationDraftKey, JSON.stringify({ coffeeId, profileId, station, notes })) } catch { /* La calibración puede continuar sin almacenamiento local. */ }
  }, [coffeeId, notes, profileId, station])
  const applicableProfiles = profiles.filter((profile) => !coffeeId || profile.coffeeIds?.includes(coffeeId))
  return <Modal isOpen={isOpen} onOpenChange={onOpenChange} title="Iniciar puesta a punto"><form className={styles.formGrid} onSubmit={(event) => { event.preventDefault(); onSubmit({ coffeeId, profileId: profileId || null, context: { station: station || null, notes: notes || null } }) }}>
    <p className={`${styles.formHint} ${styles.formFull}`}>{initialDraft ? 'Recuperamos los datos que habías dejado sin terminar en este navegador.' : 'El café y el perfil iniciales provienen de la preparación más reciente.'}</p>
    <Field label="Café"><Select required value={coffeeId} onChange={(event) => { setCoffeeId(event.target.value); setProfileId('') }}><option value="">Selecciona un café</option>{coffees.map((coffee) => <option value={coffee.id} key={coffee.id}>{coffeeName(coffee)}</option>)}</Select></Field>
    <Field label="Perfil previo" description="Opcional"><Select value={profileId} onChange={(event) => setProfileId(event.target.value)}><option value="">Sin perfil previo</option>{applicableProfiles.map((profile) => <option value={profile.id} key={profile.id}>{profile.name} · versión {profile.version}</option>)}</Select></Field>
    <Field label="Lugar de trabajo" description="Opcional"><Input value={station} onChange={(event) => setStation(event.target.value)} placeholder="Barra principal"/></Field>
    <div className={styles.formFull}><Field label="Notas iniciales" description="Opcional"><TextArea value={notes} onChange={(event) => setNotes(event.target.value)}/></Field></div>
    {error ? <div className={styles.formFull}><Alert variant="danger" title="No se pudo iniciar">{error.message}</Alert></div> : null}
    <div className={`${styles.actions} ${styles.formFull}`}><Button variant="quiet" onPress={() => onOpenChange(false)}>Cancelar</Button><Button type="submit" isDisabled={pending || !coffeeId}>{pending ? 'Iniciando…' : 'Iniciar puesta a punto'}</Button></div>
  </form></Modal>
}
