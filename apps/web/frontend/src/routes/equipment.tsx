import { useState } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { api, post } from '@/shared/api/client'
import type { Equipment, Page } from '@/shared/api/types'
import { Alert, Button, Checkbox, Field, Input, Modal, Select, Surface, useToast } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import { profileProperties, profilePropertyLabel } from './profile-options'
import styles from './routes.module.css'

const emptyForm = { id: '', type: 'machine', name: '', location: '', capabilityProperty: '', manual: false, automatic: false, sensorId: '', measuredProperty: '', sensorLocation: '', accuracy: '', accuracyUnit: '', frequency: '', calibratedAt: '' }

export default function EquipmentRoute() {
  const [open, setOpen] = useState(false)
  const [form, setForm] = useState(emptyForm)
  const queryClient = useQueryClient()
  const toast = useToast()
  const equipment = useQuery({ queryKey: ['equipment'], queryFn: () => api<Page<Equipment>>('/api/v1/equipment') })
  const create = useMutation({ mutationFn: () => post<Equipment>('/api/v1/equipment', {
    id: form.id, type: form.type, name: form.name, location: form.location,
    capabilities: form.capabilityProperty && (form.manual || form.automatic) ? [{ property: form.capabilityProperty, manualAdjustment: form.manual, automaticAdjustment: form.automatic }] : [],
    sensors: form.sensorId ? [{ id: form.sensorId, measuredProperty: form.measuredProperty, location: form.sensorLocation, accuracy: Number(form.accuracy), accuracyUnit: form.accuracyUnit, samplingFrequencyHz: Number(form.frequency), lastCalibrationAt: new Date(form.calibratedAt).toISOString() }] : [],
  }), onSuccess: async () => { await queryClient.invalidateQueries({ queryKey: ['equipment'] }); setOpen(false); setForm(emptyForm); toast.push('Equipo registrado', 'El catálogo quedó actualizado.', 'success') } })
  const update = (field: keyof typeof emptyForm, value: string | boolean) => setForm((current) => ({ ...current, [field]: value }))
  return <div className={styles.page}>
    <PageHeading title="Equipos y sensores"/>
    <div className={styles.headingAction}><Button icon="plus" onPress={() => setOpen(true)}>Registrar equipo</Button></div>
    {equipment.error ? <Alert variant="danger" title="No se pudo cargar el catálogo">{equipment.error.message}</Alert> : null}
    <div className={styles.equipmentGrid}>{equipment.data?.items.map((item) => <Surface key={item.id}><span className={styles.kicker}>{item.type === 'machine' ? 'Máquina' : 'Molino'}</span><h2 className={styles.surfaceTitle}>{item.name}</h2><p className={styles.meta}>{item.id} · {item.location}</p><h3>Capacidades declaradas</h3>{item.capabilities.length ? <ul className={styles.equipmentList}>{item.capabilities.map((capability) => <li key={capability.property}><strong>{profilePropertyLabel(capability.property)}</strong><span>{[capability.manualAdjustment ? 'Ajuste manual' : '', capability.automaticAdjustment ? 'Ajuste automático' : ''].filter(Boolean).join(' · ')}</span></li>)}</ul> : <p className={styles.muted}>Sin información declarada.</p>}<h3>Sensores</h3>{item.sensors.length ? <ul className={styles.equipmentList}>{item.sensors.map((sensor) => <li key={sensor.id}><strong>{profilePropertyLabel(sensor.measuredProperty)}</strong><span>{sensor.id} · {sensor.location}</span><small>Precisión {sensor.accuracy} {sensor.accuracyUnit} · {sensor.samplingFrequencyHz} Hz · Calibrado {formatDateTime(sensor.lastCalibrationAt)}</small></li>)}</ul> : <p className={styles.muted}>No hay sensores registrados.</p>}</Surface>)}</div>
    <Modal isOpen={open} onOpenChange={setOpen} title="Registrar equipo"><form className={styles.equipmentForm} onSubmit={(event) => { event.preventDefault(); create.mutate() }}>
      <Field label="Identificador"><Input required value={form.id} onChange={(event) => update('id', event.target.value)}/></Field><Field label="Tipo"><Select value={form.type} onChange={(event) => update('type', event.target.value)}><option value="machine">Máquina</option><option value="grinder">Molino</option></Select></Field><Field label="Nombre"><Input required value={form.name} onChange={(event) => update('name', event.target.value)}/></Field><Field label="Ubicación"><Input required value={form.location} onChange={(event) => update('location', event.target.value)}/></Field>
      <fieldset className={styles.formFull}><legend>Capacidad de ajuste opcional</legend><Field label="Propiedad"><Select value={form.capabilityProperty} onChange={(event) => update('capabilityProperty', event.target.value)}><option value="">Sin información declarada</option>{profileProperties.map((property) => <option value={property.value} key={property.value}>{property.label}</option>)}</Select></Field><div className={styles.actions}><Checkbox label="Ajuste manual" checked={form.manual} onChange={(event) => update('manual', event.target.checked)}/><Checkbox label="Ajuste automático" checked={form.automatic} onChange={(event) => update('automatic', event.target.checked)}/></div></fieldset>
      <fieldset className={styles.formFull}><legend>Sensor opcional</legend><div className={styles.equipmentSensorFields}><Field label="Identificador del sensor"><Input value={form.sensorId} onChange={(event) => update('sensorId', event.target.value)}/></Field><Field label="Propiedad medida"><Select required={Boolean(form.sensorId)} value={form.measuredProperty} onChange={(event) => update('measuredProperty', event.target.value)}><option value="">Selecciona una propiedad</option>{profileProperties.map((property) => <option value={property.value} key={property.value}>{property.label}</option>)}</Select></Field><Field label="Ubicación de medición"><Input required={Boolean(form.sensorId)} value={form.sensorLocation} onChange={(event) => update('sensorLocation', event.target.value)}/></Field><Field label="Precisión"><Input required={Boolean(form.sensorId)} type="number" min="0" step="any" value={form.accuracy} onChange={(event) => update('accuracy', event.target.value)}/></Field><Field label="Unidad de precisión"><Input required={Boolean(form.sensorId)} value={form.accuracyUnit} onChange={(event) => update('accuracyUnit', event.target.value)}/></Field><Field label="Frecuencia (Hz)"><Input required={Boolean(form.sensorId)} type="number" min="0" step="any" value={form.frequency} onChange={(event) => update('frequency', event.target.value)}/></Field><Field label="Última calibración"><Input required={Boolean(form.sensorId)} type="datetime-local" value={form.calibratedAt} onChange={(event) => update('calibratedAt', event.target.value)}/></Field></div></fieldset>
      {create.error ? <div className={styles.formFull}><Alert variant="danger" title="No se pudo registrar">{create.error.message}</Alert></div> : null}<div className={`${styles.actions} ${styles.formFull}`}><Button type="submit" isDisabled={create.isPending}>Guardar equipo</Button></div>
    </form></Modal>
  </div>
}
