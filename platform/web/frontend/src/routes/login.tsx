import { useState, type FormEvent } from 'react'
import { ApiError } from '@/shared/api/client'
import { Button, Field, Icon, Input } from '@/shared/ui'
import { useAuth } from '@/app/auth-context'
import styles from './login.module.css'

export default function Login() {
  const { login } = useAuth()
  const [username, setUsername] = useState('')
  const [password, setPassword] = useState('')
  const [error, setError] = useState('')
  const [submitting, setSubmitting] = useState(false)

  const submit = async (event: FormEvent) => {
    event.preventDefault()
    setError('')
    setSubmitting(true)
    try {
      await login(username, password)
    } catch (caught) {
      setError(caught instanceof ApiError ? caught.message : 'No fue posible iniciar sesión.')
    } finally {
      setSubmitting(false)
    }
  }

  return <main className={styles.page}>
    <section className={styles.intro} aria-label="Bitácora de espresso">
      <div className={styles.brand}><span><Icon name="coffee" size={24}/></span>Bitácora de espresso</div>
      <div className={styles.introCopy}>
        <p>Preparaciones consistentes</p>
        <h1>Tu registro de espresso, en un solo lugar.</h1>
      </div>
    </section>
    <section className={styles.access}>
      <form className={styles.form} onSubmit={(event) => void submit(event)}>
        <header><p>Bienvenido</p><h2>Inicia sesión</h2></header>
        <Field label="Usuario" id="username">
          <Input id="username" name="username" autoComplete="username" value={username} onChange={(event) => setUsername(event.target.value)} required autoFocus/>
        </Field>
        <Field label="Contraseña" id="password">
          <Input id="password" name="password" type="password" autoComplete="current-password" value={password} onChange={(event) => setPassword(event.target.value)} required minLength={8}/>
        </Field>
        {error ? <p className={styles.error} role="alert">{error}</p> : null}
        <Button type="submit" isDisabled={submitting}>{submitting ? 'Ingresando…' : 'Ingresar'}</Button>
      </form>
    </section>
  </main>
}
