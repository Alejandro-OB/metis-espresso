import { useEffect, type ReactNode } from 'react'
import { useQuery } from '@tanstack/react-query'
import { api } from '@/shared/api/client'
import type { Coffee, Equipment, Page } from '@/shared/api/types'
import { Field, NumberField, Select, TextArea } from '@/shared/ui'
import { CoffeePicker } from './coffee-picker'
import type { PreparationContext } from './preparation-context'
import styles from './routes.module.css'

// `withNotes` en falso deja las notas a quien arma el formulario: al corregir
// un café van al final, después de las medidas, y no entre los datos.
export function ContextFields({ value, onChange, autoEquipment = false, withNotes = true, leading, middle }: { value: PreparationContext; onChange: (value: PreparationContext) => void; autoEquipment?: boolean; withNotes?: boolean; leading?: ReactNode; middle?: ReactNode }) {
  const equipment = useQuery({ queryKey: ['equipment'], queryFn: () => api<Page<Equipment>>('/api/v1/equipment') })
  const machines = (equipment.data?.items ?? []).filter((item) => item.type === 'machine')
  const grinders = (equipment.data?.items ?? []).filter((item) => item.type === 'grinder')
  const coffees = useQuery({ queryKey: ['coffees', 'picker'], queryFn: () => api<Page<Coffee>>('/api/v1/coffees?page=1&page_size=100'), enabled: autoEquipment })
  const set = (changes: Partial<PreparationContext>) => onChange({ ...value, ...changes })
  // Con una sola máquina y un solo molino registrados no hay nada que elegir: se
  // asignan solos y el barista no los ve (pedido del usuario del 23-oct-2026).
  // Con más de uno, elegir por él afirmaría un equipo que nadie indicó, así que
  // el selector vuelve a aparecer. Solo en un café nuevo: en uno ya hecho,
  // llenarlo afirmaría un equipo que nadie anotó entonces. Sin ninguno
  // registrado tampoco hay qué elegir: el selector solo ofrecería «Sin
  // registrar» y el café queda sin equipo, que es lo que consta.
  const onlyMachine = autoEquipment && machines.length === 1 ? machines[0] : undefined
  const onlyGrinder = autoEquipment && grinders.length === 1 ? grinders[0] : undefined
  // El café, en cambio, se llena pero sigue a la vista: con uno solo
  // registrado es casi siempre ese, y el barista puede cambiarlo o crear otro
  // ahí mismo, cosa que con los equipos no pasa.
  const coffeeItems = coffees.data?.items ?? []
  const onlyCoffee = autoEquipment && coffeeItems.length === 1 ? coffeeItems[0] : undefined
  // Sin elección posible tampoco al corregir un café ya hecho: sin equipos
  // registrados, o con uno solo que es el que el café ya tiene (o ninguno),
  // el selector solo ofrecería «Sin registrar» o lo que ya está.
  const hideMachine = Boolean(onlyMachine) || !machines.length || (machines.length === 1 && (!value.machineId || value.machineId === machines[0].id))
  const hideGrinder = Boolean(onlyGrinder) || !grinders.length || (grinders.length === 1 && (!value.grinderId || value.grinderId === grinders[0].id))
  useEffect(() => {
    const changes: Partial<PreparationContext> = {}
    if (onlyCoffee && !value.coffeeId) changes.coffeeId = onlyCoffee.id
    if (onlyMachine && !value.machineId) changes.machineId = onlyMachine.id
    if (onlyGrinder && !value.grinderId) changes.grinderId = onlyGrinder.id
    if (Object.keys(changes).length) onChange({ ...value, ...changes })
  }, [onlyCoffee, onlyMachine, onlyGrinder, value, onChange])
  // Todas del mismo ancho, el de la más larga (el tipo de canastilla), en
  // filas de tres centradas (24-oct-2026). A media tarjeta cada una, un número
  // de dos cifras ocupaba 600 px; con anchos distintos las filas quedaban
  // desparejas.
  return <><div className={styles.uniformFields}>
    {leading}
    <div className={styles.fieldUniform}><CoffeePicker value={value.coffeeId} onChange={(coffeeId) => set({ coffeeId })}/></div>
    {hideMachine ? null : <div className={styles.fieldUniform}><Field label="Máquina"><Select value={value.machineId} onChange={(event) => set({ machineId: event.target.value })}><option value="">Sin registrar</option>{machines.map((item) => <option value={item.id} key={item.id}>{item.name}</option>)}</Select></Field></div>}
    {hideGrinder ? null : <div className={styles.fieldUniform}><Field label="Molino"><Select value={value.grinderId} onChange={(event) => set({ grinderId: event.target.value })}><option value="">Sin registrar</option>{grinders.map((item) => <option value={item.id} key={item.id}>{item.name}</option>)}</Select></Field></div>}
    <div className={styles.fieldUniform}><Field label="Molienda (clics)"><NumberField min="0" step="1" value={value.grindSetting} onChange={(event) => set({ grindSetting: event.target.value })}/></Field></div>
    <div className={styles.fieldUniform}><Field label="Tipo de canastilla"><Select value={value.portafilterConfiguration} onChange={(event) => set({ portafilterConfiguration: event.target.value })}><option value="pressurized-double-coffee">Canastilla de dos tazas presurizada</option><option value="non-pressurized-double-coffee">Canastilla de dos tazas no presurizada</option><option value="no-basket-water">Sin canastilla</option><option value="">Sin registrar</option></Select></Field></div>
    {middle}
    {/* Los mismos rangos que acepta el servidor, que son los de la preparación simulada. */}
    <div className={styles.fieldUniform}><Field label="Temperatura del cuarto (°C)"><NumberField placeholder="opcional" min="-20" max="60" step="0.1" value={value.ambientTemperatureC} onChange={(event) => set({ ambientTemperatureC: event.target.value })}/></Field></div>
    <div className={styles.fieldUniform}><Field label="Humedad del cuarto (%)"><NumberField placeholder="opcional" min="0" max="100" step="1" value={value.ambientHumidityPercent} onChange={(event) => set({ ambientHumidityPercent: event.target.value })}/></Field></div>
    {/* La sesión de catación (sesión, código ciego, bloque, condición) ya no
        se pide (23-oct-2026): nada la usa para calcular y el protocolo
        vigente no la necesita. Un café que la traiga la conserva, porque el
        contexto la reenvía tal como llegó. */}
  </div>
    {withNotes ? <div className={styles.compactNotes}><Field label="Notas"><TextArea rows={2} value={value.notes} onChange={(event) => set({ notes: event.target.value })}/></Field></div> : null}
  </>
}
