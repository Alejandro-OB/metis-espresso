import { useState, type Key } from 'react'
import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { Link, useNavigate } from 'react-router-dom'
import { api, post } from '@/shared/api/client'
import type { Coffee, Page, PreparationProfile } from '@/shared/api/types'
import { Alert, Button, DataTable, Modal, Pagination, Surface, type Column, useToast } from '@/shared/ui'
import { PageHeading } from './page-heading'
import { ProfileForm, type ProfilePayload } from './profile-form'
import styles from './routes.module.css'

const statusLabels: Record<string, string> = { draft: 'Borrador', approved: 'Aprobado', retired: 'Retirado', proposed: 'Propuesto', accepted: 'Aprobado', effective: 'Vigente' }

export default function ProfilesRoute() {
  const [page, setPage] = useState(1)
  const [open, setOpen] = useState(false)
  const [selected, setSelected] = useState<Set<Key>>(new Set())
  const navigate = useNavigate()
  const queryClient = useQueryClient()
  const toast = useToast()
  const profiles = useQuery({ queryKey: ['profiles', page], queryFn: () => api<Page<PreparationProfile>>(`/api/v1/profiles?page=${page}`) })
  const coffees = useQuery({ queryKey: ['coffees', 'profile-options'], queryFn: () => api<Page<Coffee>>('/api/v1/coffees?page=1&page_size=100') })
  const create = useMutation({ mutationFn: (payload: ProfilePayload) => post<PreparationProfile>('/api/v1/profiles', payload), onSuccess: async () => { await queryClient.invalidateQueries({ queryKey: ['profiles'] }); setOpen(false); toast.push('Perfil creado', 'El perfil quedó guardado como borrador.', 'success') } })
  const columns: Column<PreparationProfile>[] = [
    { id: 'name', header: 'Perfil', cell: (item) => <Link to={`/profiles/${item.id}`}>{item.name}</Link>, sortable: true, sortValue: (item) => item.name },
    { id: 'version', header: 'Versión', cell: (item) => item.version, numeric: true, sortable: true, sortValue: (item) => item.version },
    { id: 'status', header: 'Estado', cell: (item) => statusLabels[item.status] ?? item.status },
    { id: 'coffees', header: 'Cafés', cell: (item) => item.coffeeIds?.length ?? 0, numeric: true },
    { id: 'specifications', header: 'Variables', cell: (item) => item.specifications?.length ?? 0, numeric: true },
  ]
  return <div className={styles.page}>
    <PageHeading title="Perfiles de preparación" />
    <Surface><div className={styles.listActions}><Button variant="secondary" icon="compare" isDisabled={selected.size < 2 || selected.size > 4} onPress={() => void navigate(`/profiles/compare?ids=${[...selected].map(String).join(',')}`)}>Comparar ({selected.size})</Button><Button icon="plus" onPress={() => setOpen(true)}>Crear perfil</Button></div>{profiles.error ? <Alert variant="danger" title="No fue posible cargar los perfiles">{profiles.error.message}</Alert> : <DataTable label="Perfiles de preparación" rows={profiles.data?.items ?? []} columns={columns} getKey={(item) => item.id} loading={profiles.isLoading} selectedKeys={selected} onSelectionChange={setSelected}/>}<Pagination page={profiles.data?.page ?? page} pages={profiles.data?.pages ?? 1} onChange={setPage}/></Surface>
    <Modal isOpen={open} onOpenChange={setOpen} title="Nuevo perfil de preparación">{coffees.error ? <Alert variant="danger" title="No fue posible cargar los cafés">{coffees.error.message}</Alert> : <ProfileForm coffees={coffees.data?.items ?? []} pending={create.isPending} onSubmit={(payload) => create.mutate(payload)}/>} {create.error ? <Alert variant="danger" title="No se pudo crear el perfil">{create.error.message}</Alert> : null}</Modal>
  </div>
}
