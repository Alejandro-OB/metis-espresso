import { useEffect, useMemo, useRef, useState } from 'react'
import { useMutation, useQuery } from '@tanstack/react-query'
import { useNavigate } from 'react-router-dom'
import { api, post } from '@/shared/api/client'
import type { Coffee, DashboardResponse, Equipment, LiveExtraction, Page, PreparationProfile } from '@/shared/api/types'
import { Alert, Button, Field, NumberField, Select, Surface } from '@/shared/ui'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

function coffeeName(coffee: Coffee) {
  return coffee.commercialReference || coffee.lotIdentifier || coffee.bagIdentifier || coffee.id
}

export default function StartExtractionRoute() {
  const navigate = useNavigate()
  const [coffeeId, setCoffeeId] = useState('')
  const [profileId, setProfileId] = useState('')
  const [temperature, setTemperature] = useState('')
  const [humidity, setHumidity] = useState('')
  const [machineId, setMachineId] = useState('')
  const [grinderId, setGrinderId] = useState('')
  const [calibrationId, setCalibrationId] = useState('')
  const initialized = useRef(false)
  const coffees = useQuery({ queryKey: ['coffees', 'live-options'], queryFn: () => api<Page<Coffee>>('/api/v1/coffees?page=1&page_size=100') })
  const profiles = useQuery({ queryKey: ['profiles', 'live-options'], queryFn: () => api<Page<PreparationProfile>>('/api/v1/profiles?page=1&page_size=100') })
  const equipment = useQuery({ queryKey: ['equipment'], queryFn: () => api<Page<Equipment>>('/api/v1/equipment') })
  const dashboard = useQuery({ queryKey: ['dashboard'], queryFn: () => api<DashboardResponse>('/api/v1/dashboard') })
  useEffect(() => {
    if (initialized.current || !dashboard.data) return
    setCoffeeId(dashboard.data.activeCoffee?.id ?? '')
    setProfileId(dashboard.data.activeProfile?.id ?? '')
    setMachineId(dashboard.data.activeEquipment.machineId ?? '')
    setGrinderId(dashboard.data.activeEquipment.grinderId ?? '')
    setCalibrationId(dashboard.data.activeEquipment.calibrationId ?? '')
    initialized.current = true
  }, [dashboard.data])
  const availableProfiles = useMemo(() => (profiles.data?.items ?? []).filter((profile) => profile.status === 'approved' && (!coffeeId || profile.coffeeIds.includes(coffeeId))), [coffeeId, profiles.data?.items])
  const machines = (equipment.data?.items ?? []).filter((item) => item.type === 'machine')
  const grinders = (equipment.data?.items ?? []).filter((item) => item.type === 'grinder')
  const start = useMutation({
    mutationFn: () => post<LiveExtraction>('/api/v1/extractions/live', {
      coffeeId,
      profileId,
      ambientTemperatureC: temperature ? Number(temperature) : null,
      ambientHumidityPercent: humidity ? Number(humidity) : null,
      machineId: machineId || null,
      grinderId: grinderId || null,
      calibrationId: calibrationId || null,
    }),
    onSuccess: (item) => void navigate(`/extract/${item.id}`),
  })

  return <div className={styles.page}>
    <PageHeading title="Preparación de práctica" detail="Datos de demostración; la máquina no se acciona."/>
    <Surface className={styles.startExtractionCard}>
      <form className={styles.formGrid} onSubmit={(event) => { event.preventDefault(); start.mutate() }}>
        {dashboard.data?.activeCoffee ? <p className={`${styles.formHint} ${styles.formFull}`}>El café, perfil y equipo iniciales provienen de la preparación más reciente. Puedes cambiarlos antes de comenzar.</p> : null}
        <Field label="Café"><Select value={coffeeId} required onChange={(event) => { setCoffeeId(event.target.value); setProfileId('') }}><option value="">Selecciona un café</option>{(coffees.data?.items ?? []).map((coffee) => <option value={coffee.id} key={coffee.id}>{coffeeName(coffee)}</option>)}</Select></Field>
        <Field label="Perfil de preparación"><Select value={profileId} required disabled={!coffeeId} onChange={(event) => setProfileId(event.target.value)}><option value="">Selecciona un perfil</option>{availableProfiles.map((profile) => <option value={profile.id} key={profile.id}>{profile.name} · versión {profile.version}</option>)}</Select></Field>
        <Field label="Temperatura ambiente (°C)" description="Opcional"><NumberField min={-20} max={60} step="0.1" value={temperature} onChange={(event) => setTemperature(event.target.value)}/></Field>
        <Field label="Humedad ambiente (%)" description="Opcional"><NumberField min={0} max={100} step="0.1" value={humidity} onChange={(event) => setHumidity(event.target.value)}/></Field>
        <Field label="Máquina" description="Opcional"><Select value={machineId} onChange={(event) => setMachineId(event.target.value)}><option value="">Sin registrar</option>{machines.map((item) => <option value={item.id} key={item.id}>{item.name}</option>)}</Select></Field>
        <Field label="Molino" description="Opcional"><Select value={grinderId} onChange={(event) => setGrinderId(event.target.value)}><option value="">Sin registrar</option>{grinders.map((item) => <option value={item.id} key={item.id}>{item.name}</option>)}</Select></Field>
        <Field label="Puesta a punto" description="Opcional"><Select value={calibrationId} onChange={(event) => setCalibrationId(event.target.value)}><option value="">Sin registrar</option>{dashboard.data?.lastCalibration ? <option value={dashboard.data.lastCalibration.id}>Última puesta a punto</option> : null}</Select></Field>
        {start.error ? <div className={styles.formFull}><Alert variant="danger" title="No fue posible iniciar">{start.error.message}</Alert></div> : null}
        <div className={`${styles.actions} ${styles.formFull}`}><Button type="submit" isDisabled={!coffeeId || !profileId || start.isPending}>{start.isPending ? 'Iniciando…' : 'Comenzar práctica'}</Button></div>
      </form>
    </Surface>
  </div>
}
