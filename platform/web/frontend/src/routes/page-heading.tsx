import type { ReactNode } from 'react'
import styles from './routes.module.css'

/* Encabezado común de todas las rutas: el título y, si hace falta, una línea
   de datos (fecha, versión, estado). El rótulo que iba sobre el título y las
   explicaciones largas se quitaron a pedido del usuario (19-oct-2026): ocupaban
   la parte de arriba de cada pantalla sin decir nada que el menú y el título no
   dijeran ya. Lo que queda debajo es dato de ese registro, no explicación.
   La acción principal de la página, si la hay, va a la derecha del título:
   así se ve sin bajar y sin flotar encima del contenido. */
export function PageHeading({ title, detail, hero = false, action }: { title: string; detail?: string; hero?: boolean; action?: ReactNode }) {
  const heading = <header className={styles.heading}>
    <h1 className={hero ? styles.heroTitle : styles.title}>{title}</h1>
    {detail ? <p className={styles.lead}>{detail}</p> : null}
  </header>
  return action ? <div className={styles.headingWithAction}>{heading}<div>{action}</div></div> : heading
}
