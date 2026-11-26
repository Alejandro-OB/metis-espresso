import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { api, post } from '@/shared/api/client'
import type { Coffee, Page } from '@/shared/api/types'
import { SearchSelect } from '@/shared/ui'
import { coffeeName } from './preparation-context'
import styles from './routes.module.css'

// Un solo campo para el café: se escribe, se elige entre los que coinciden y,
// si no existe, se crea con el nombre escrito. Los demás datos del café se
// completan después en Cafés; aquí no hacen falta para preparar.
export function CoffeePicker({ value, onChange }: { value: string; onChange: (coffeeId: string) => void }) {
  const queryClient = useQueryClient()
  const coffees = useQuery({ queryKey: ['coffees', 'picker'], queryFn: () => api<Page<Coffee>>('/api/v1/coffees?page=1&page_size=100') })
  const create = useMutation({
    mutationFn: (name: string) => post<Coffee>('/api/v1/coffees', { commercialReference: name }),
    onSuccess: async (coffee) => { await queryClient.invalidateQueries({ queryKey: ['coffees'] }); onChange(coffee.id) },
  })
  return <div>
    <SearchSelect label="Café" placeholder="Escribe para buscar o crear" value={value} onChange={onChange}
      options={(coffees.data?.items ?? []).map((coffee) => ({ id: coffee.id, label: coffeeName(coffee) }))}
      onCreate={(name) => create.mutate(name)} createLabel={(name) => `Crear café «${name}»`} creating={create.isPending}/>
    {create.error ? <p role="alert" className={styles.formError}>{create.error.message}</p> : null}
  </div>
}
