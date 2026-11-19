import { useMutation, useQuery, useQueryClient } from '@tanstack/react-query'
import { api, post } from '@/shared/api/client'
import { Alert, Button, Surface } from '@/shared/ui'
import styles from './routes.module.css'

/**
 * Sostenimiento del objetivo (ADR-018): la subida de caldera del primer uso que
 * llevó una preparación y la exclusión que el barista puede pedir para un
 * perfil.
 *
 * La subida la decide la plataforma antes del café y la aplica la placa; aquí
 * solo se muestra la que la placa declara haber aplicado. La corrección después
 * de cada café se retiró el 2026-10-22 (ADR-018 § 13), y con ella su decisión
 * por preparación. Tampoco suspende el sostenimiento, que es decisión del
 * protocolo experimental y se hace desde la administración.
 */

export function SustainmentPanel({ offsetC }: { offsetC: number | null | undefined }) {
  return <Surface>
    <h2 className={styles.surfaceTitle}>Sostenimiento del objetivo</h2>
    {offsetC
      ? <p className={styles.formHint}>Primer café tras un reposo: la máquina subió la caldera {offsetC.toFixed(1)} °C para que el agua llegara a la temperatura del perfil.</p>
      : <p className={styles.muted}>Esta preparación no llevó subida de caldera: la máquina no venía de un reposo, o el perfil estaba excluido o el sostenimiento suspendido.</p>}
  </Surface>
}

export function SustainmentExclusionControl({ profileId }: { profileId: string }) {
  const queryClient = useQueryClient()
  const state = useQuery({
    queryKey: ['sustainment-exclusion', profileId],
    queryFn: () => api<{ excluded: boolean }>(`/api/v1/profiles/${profileId}/sustainment-exclusion`),
  })
  const toggle = useMutation({
    mutationFn: (exclude: boolean) => exclude
      ? post(`/api/v1/profiles/${profileId}/sustainment-exclusion`, {})
      : api(`/api/v1/profiles/${profileId}/sustainment-exclusion`, { method: 'DELETE' }),
    onSuccess: () => { void queryClient.invalidateQueries({ queryKey: ['sustainment-exclusion', profileId] }) },
  })
  const excluded = state.data?.excluded ?? false
  return <Surface>
    <h2 className={styles.surfaceTitle}>Sostenimiento del objetivo</h2>
    <p className={styles.formHint}>{excluded
      ? 'Este perfil está excluido: el sistema no subirá la caldera antes del primer café tras un reposo.'
      : 'Antes del primer café tras un reposo, el sistema sube la caldera para que el agua llegue a la temperatura de este perfil. Exclúyelo si quieres ver cómo responde la máquina sin ayuda.'}</p>
    {toggle.error ? <Alert variant="danger" title="No fue posible cambiar la exclusión">{toggle.error.message}</Alert> : null}
    <Button variant="secondary" onPress={() => toggle.mutate(!excluded)} isDisabled={state.isLoading || toggle.isPending}>
      {excluded ? 'Volver a incluir' : 'Excluir del sostenimiento'}
    </Button>
  </Surface>
}
