import { useState } from 'react'
import { useMutation, useQueries, useQueryClient } from '@tanstack/react-query'
import { Link, useParams } from 'react-router-dom'
import { api, put } from '@/shared/api/client'
import type { Coffee, CoffeeOptionalField, Extraction, Page } from '@/shared/api/types'
import { Alert, Breadcrumbs, Button, LoadingState, Modal, Surface, useToast } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import { DeclaredSensoryForm, type DeclaredProfilePayload } from './sensory-profile-forms'
import { CoffeeModal } from './coffees'
import { sensoryDescriptorLabel } from './sensory-options'
import styles from './routes.module.css'

export default function CoffeeDetailRoute() {
  const { id = '' } = useParams()
  const [declaredOpen, setDeclaredOpen] = useState(false)
  const [editOpen, setEditOpen] = useState(false)
  const queryClient = useQueryClient()
  const toast = useToast()
  const [coffee, extractions] = useQueries({ queries: [
    { queryKey: ['coffee', id], queryFn: () => api<Coffee>(`/api/v1/coffees/${id}`) },
    { queryKey: ['extractions', 1, id], queryFn: () => api<Page<Extraction>>(`/api/v1/extractions?page=1&page_size=100&query=${encodeURIComponent(id)}`) },
  ] })
  const declared = useMutation({
    mutationFn: (payload: DeclaredProfilePayload) => put<Coffee>(`/api/v1/coffees/${id}/declared-sensory-profile`, payload),
    onSuccess: async () => {
      await queryClient.invalidateQueries({ queryKey: ['coffee', id] })
      setDeclaredOpen(false)
      toast.push('Descripción guardada', 'La declaración quedó asociada al café.', 'success')
    },
  })
  const edit = useMutation({
    mutationFn: (payload: Record<string, unknown>) => put<Coffee>(`/api/v1/coffees/${id}`, payload),
    onSuccess: async () => {
      await queryClient.invalidateQueries({ queryKey: ['coffee', id] })
      await queryClient.invalidateQueries({ queryKey: ['coffees'] })
      setEditOpen(false)
      toast.push('Café corregido', 'Lo que decía antes queda en su historial.', 'success')
    },
  })
  if (coffee.isLoading || extractions.isLoading) return <LoadingState />
  if (coffee.error || !coffee.data) return <Alert variant="danger" title="No se encontró el café">{coffee.error?.message ?? 'Sin datos'}</Alert>
  const item = coffee.data
  const name = coffeeName(item)
  const declaredProfile = item.declaredSensoryProfile
  return <div className={styles.page}>
    <Breadcrumbs items={[{ label: 'Cafés', to: '/coffees' }, { label: name }]}/>
    <PageHeading title={name}/>
    <div className={styles.profileDetailActions}><Button variant="secondary" onPress={() => setEditOpen(true)}>Editar datos del café</Button></div>
    <div className={styles.grid}>
      <Surface className={styles.span6}><h2 className={styles.surfaceTitle}>Identificación</h2><dl className={styles.definition}><CoffeeDatum coffee={item} field="commercialReference" label="Referencia comercial"/><CoffeeDatum coffee={item} field="lotIdentifier" label="Lote"/><CoffeeDatum coffee={item} field="bagIdentifier" label="Bolsa"/><CoffeeDatum coffee={item} field="roastDate" label="Fecha de tueste" format={formatDate}/><CoffeeDatum coffee={item} field="variety" label="Variedad"/><CoffeeDatum coffee={item} field="postHarvestProcess" label="Proceso poscosecha"/><CoffeeDatum coffee={item} field="roastLevel" label="Nivel de tueste"/></dl></Surface>
      <Surface className={styles.span6}><h2 className={styles.surfaceTitle}>Origen</h2><dl className={styles.definition}><CoffeeDatum coffee={item} field="region" label="Región"/><CoffeeDatum coffee={item} field="altitudeM" label="Altitud" format={(value) => `${value} m s. n. m.`}/><CoffeeDatum coffee={item} field="soilCharacteristics" label="Suelo"/><CoffeeDatum coffee={item} field="mineralComposition" label="Composición mineral"/><CoffeeDatum coffee={item} field="mineralSource" label="Fuente de minerales"/></dl></Surface>
      <Surface className={styles.span12}>
        <div className={styles.sectionHeading}><div><h2 className={styles.surfaceTitle}>Descripción declarada por productor o tostador</h2><p>Información proporcionada con el café; es independiente del objetivo del barista.</p></div><Button variant="secondary" onPress={() => setDeclaredOpen(true)}>{declaredProfile ? 'Editar descripción' : 'Registrar descripción'}</Button></div>
        {declaredProfile ? <div className={styles.stack}><p>{declaredProfile.originalText}</p><p className={styles.meta}>Declarado por {declaredProfile.declaredBy} · Fuente: {declaredProfile.source}</p><ul className={styles.list}>{declaredProfile.descriptors.map((descriptor) => <li key={descriptor}>{sensoryDescriptorLabel(descriptor)}</li>)}</ul></div> : <p className={styles.muted}>Este café no tiene una descripción sensorial declarada.</p>}
      </Surface>
      <Surface className={styles.span12}><h2 className={styles.surfaceTitle}>Preparaciones relacionadas</h2><div className={styles.stack}>{(extractions.data?.items ?? []).map((extraction) => <div className={styles.service} key={extraction.id}><div><Link to={`/extractions/${extraction.id}`} className={styles.data}>{extraction.id}</Link><p>{formatDateTime(extraction.finishedAt)}</p></div><Link to={`/extractions/${extraction.id}`} className={styles.rowLink}>Abrir <span aria-hidden="true">→</span></Link></div>)}</div></Surface>
    </div>
    {editOpen ? <CoffeeModal isOpen={editOpen} onOpenChange={setEditOpen} initial={item} title="Editar café" pending={edit.isPending} error={edit.error} onSubmit={(payload) => edit.mutate(payload)}/> : null}
    <Modal isOpen={declaredOpen} onOpenChange={setDeclaredOpen} title="Descripción sensorial declarada"><DeclaredSensoryForm key={declaredProfile?.id ?? 'new'} initial={declaredProfile} pending={declared.isPending} onSubmit={(payload) => declared.mutate(payload)}/>{declared.error ? <Alert variant="danger" title="No se pudo guardar">{declared.error.message}</Alert> : null}</Modal>
  </div>
}

function coffeeName(coffee: Coffee) {
  return coffee.commercialReference || coffee.lotIdentifier || coffee.bagIdentifier || 'Café sin nombre'
}

function formatDate(value: string | number) {
  return new Intl.DateTimeFormat('es-CO', { dateStyle: 'medium', timeZone: 'UTC' }).format(new Date(String(value)))
}

function CoffeeDatum({ coffee, field, label, format = String }: { coffee: Coffee; field: CoffeeOptionalField; label: string; format?: (value: string | number) => string }) {
  const value = coffee[field]
  let display = 'Sin información'
  if (coffee.notApplicableFields?.includes(field)) display = 'No aplica'
  else if (typeof value === 'string' || typeof value === 'number') display = format(value)
  return <><dt>{label}</dt><dd>{display}</dd></>
}
