import { useState } from 'react'
import { useMutation, useQueries, useQueryClient } from '@tanstack/react-query'
import { api, post, put } from '@/shared/api/client'
import type { AdminUser, Page, StatusResponse } from '@/shared/api/types'
import { Alert, Button, DataTable, Field, Input, Select, ServiceStatus, Surface, useToast, type Column } from '@/shared/ui'
import { formatDateTime } from '@/shared/lib/format'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

interface Audit { id: number; actor: string; operation: string; resource: string; result: string; correlationId: string; occurredAt: string }
interface SystemInfo { application: { version: string }; ontology: { name: string; version: string } }
const emptyAccount = { displayName: '', username: '', password: '', role: 'barista' as 'barista' | 'admin' }

export default function SystemRoute() {
  const [account, setAccount] = useState(emptyAccount)
  const queryClient = useQueryClient()
  const toast = useToast()
  const [status, system, audit, users] = useQueries({ queries: [
    { queryKey: ['status'], queryFn: () => api<StatusResponse>('/api/v1/status') },
    { queryKey: ['system'], queryFn: () => api<SystemInfo>('/api/v1/system') },
    { queryKey: ['audit'], queryFn: () => api<Page<Audit>>('/api/v1/audit?page=1&page_size=50') },
    { queryKey: ['admin-users'], queryFn: () => api<Page<AdminUser>>('/api/v1/admin/users?page=1&page_size=100') },
  ] })
  const create = useMutation({
    mutationFn: () => post<AdminUser>('/api/v1/admin/users', account),
    onSuccess: async () => {
      setAccount(emptyAccount)
      await Promise.all([queryClient.invalidateQueries({ queryKey: ['admin-users'] }), queryClient.invalidateQueries({ queryKey: ['audit'] })])
      toast.push('Cuenta creada', 'La persona ya puede iniciar sesión.', 'success')
    },
  })
  const update = useMutation({
    mutationFn: (item: AdminUser) => put<AdminUser>(`/api/v1/admin/users/${item.id}`, { displayName: item.displayName, role: item.role, active: !item.active }),
    onSuccess: async (item) => {
      await Promise.all([queryClient.invalidateQueries({ queryKey: ['admin-users'] }), queryClient.invalidateQueries({ queryKey: ['audit'] })])
      toast.push(item.active ? 'Cuenta activada' : 'Cuenta desactivada', 'El acceso quedó actualizado.', 'success')
    },
  })
  const userColumns: Column<AdminUser>[] = [
    { id: 'name', header: 'Nombre', cell: (item) => <strong>{item.displayName}</strong> },
    { id: 'username', header: 'Usuario', cell: (item) => item.username },
    { id: 'role', header: 'Rol', cell: (item) => item.role === 'admin' ? 'Administración' : 'Barista' },
    { id: 'state', header: 'Estado', cell: (item) => item.active ? 'Activa' : 'Inactiva' },
    { id: 'action', header: 'Acción', cell: (item) => <Button variant="quiet" isDisabled={update.isPending} onPress={() => update.mutate(item)}>{item.active ? 'Desactivar' : 'Activar'}</Button> },
  ]
  const auditColumns: Column<Audit>[] = [
    { id: 'date', header: 'Fecha', cell: (item) => formatDateTime(item.occurredAt) },
    { id: 'actor', header: 'Responsable', cell: (item) => item.actor },
    { id: 'operation', header: 'Acción', cell: (item) => item.operation },
    { id: 'result', header: 'Resultado', cell: (item) => item.result },
    { id: 'correlation', header: 'Referencia', cell: (item) => <code className={styles.data}>{item.correlationId}</code>, mobile: false },
  ]
  return <div className={styles.page}>
    <PageHeading title="Accesos y actividad"/>
    <Surface><h2 className={styles.surfaceTitle}>Cuentas</h2>
      <form className={styles.accountForm} onSubmit={(event) => { event.preventDefault(); create.mutate() }}>
        <Field label="Nombre"><Input required value={account.displayName} onChange={(event) => setAccount({ ...account, displayName: event.target.value })}/></Field>
        <Field label="Usuario"><Input required minLength={3} autoCapitalize="none" value={account.username} onChange={(event) => setAccount({ ...account, username: event.target.value })}/></Field>
        <Field label="Contraseña" description="Mínimo 12 caracteres"><Input required minLength={12} type="password" autoComplete="new-password" value={account.password} onChange={(event) => setAccount({ ...account, password: event.target.value })}/></Field>
        <Field label="Rol"><Select value={account.role} onChange={(event) => setAccount({ ...account, role: event.target.value as 'barista' | 'admin' })}><option value="barista">Barista</option><option value="admin">Administración</option></Select></Field>
        <Button type="submit" isDisabled={create.isPending}>{create.isPending ? 'Creando…' : 'Crear cuenta'}</Button>
      </form>
      {create.error ? <Alert variant="danger" title="No se pudo crear la cuenta">{create.error.message}</Alert> : null}
      {update.error ? <Alert variant="danger" title="No se pudo cambiar el acceso">{update.error.message}</Alert> : null}
      <DataTable label="Cuentas de acceso" rows={users.data?.items ?? []} columns={userColumns} getKey={(item) => item.id} loading={users.isLoading} error={users.error}/>
    </Surface>
    {system.data ? <div className={styles.grid}><Surface className={styles.span5}><h2 className={styles.surfaceTitle}>Versiones activas</h2><dl className={styles.definition}><dt>Aplicación</dt><dd>{system.data.application.version}</dd><dt>Ontología</dt><dd>{system.data.ontology.name} {system.data.ontology.version}</dd></dl></Surface><Surface className={styles.span7}><h2 className={styles.surfaceTitle}>Servicios</h2><div className={styles.services}>{status.data?.services.map((service) => <div className={styles.service} key={service.id}><div><strong>{service.label}</strong><p>{service.detail ?? service.adapter ?? 'Servicio disponible'}</p></div><ServiceStatus state={service.status}>{service.status}</ServiceStatus></div>)}</div></Surface></div> : null}
    <Surface><h2 className={styles.surfaceTitle}>Actividad administrativa</h2>{audit.error ? <Alert variant="danger" title="Actividad no disponible">{audit.error.message}</Alert> : <DataTable label="Registros de actividad" rows={audit.data?.items ?? []} columns={auditColumns} getKey={(item) => item.id} loading={audit.isLoading}/>}</Surface>
  </div>
}
