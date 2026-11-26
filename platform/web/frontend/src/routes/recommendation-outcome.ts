import type { Recommendation } from '@/shared/api/types'

// Lo que pasó con una recomendación, con las palabras de la cata
// (Seguirla, Cambiarla, No seguirla).
export const recommendationOutcome = (item: Recommendation) => item.status === 'abstained' ? 'Sin propuesta'
  : item.decision?.status === 'accepted' ? 'Seguida'
    : item.decision?.status === 'modified' ? 'Cambiada'
      : item.decision?.status === 'rejected' ? 'No seguida'
        : 'Sin decidir'
