import { LinkButton, Surface } from '@/shared/ui'
import { PageHeading } from './page-heading'
import styles from './routes.module.css'

export default function NotFoundRoute() { return <div className={styles.page}><PageHeading title="Esta página no forma parte del archivo."/><Surface><LinkButton to="/prepare" icon="arrow">Ir a preparar café</LinkButton></Surface></div> }
