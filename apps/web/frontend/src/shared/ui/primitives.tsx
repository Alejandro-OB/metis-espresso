import { useId, useState, type HTMLAttributes, type InputHTMLAttributes, type ReactNode, type SelectHTMLAttributes, type TextareaHTMLAttributes } from 'react'
import {
  Button as AriaButton,
  type ButtonProps as AriaButtonProps,
  Dialog,
  Heading,
  Modal as AriaModal,
  ModalOverlay,
  Popover as AriaPopover,
  Switch as AriaSwitch,
  Tooltip as AriaTooltip,
  TooltipTrigger,
} from 'react-aria-components'
import { Link } from 'react-router-dom'
import styles from './ui.module.css'

export type IconName = 'arrow' | 'search' | 'download' | 'close' | 'check' | 'warning' | 'info' | 'plus' | 'compare' | 'coffee' | 'history' | 'evidence' | 'system' | 'more' | 'logout'

export function Icon({ name, size = 18 }: { name: IconName; size?: number }) {
  const paths: Record<IconName, ReactNode> = {
    arrow: <><path d="M5 12h14M13 6l6 6-6 6" /></>,
    logout: <><path d="M10 5H6a1 1 0 0 0-1 1v12a1 1 0 0 0 1 1h4"/><path d="M14 8l4 4-4 4M18 12H9"/></>,
    search: <><circle cx="11" cy="11" r="6"/><path d="m16 16 4 4"/></>,
    download: <><path d="M12 3v12m0 0 5-5m-5 5-5-5M4 20h16"/></>,
    close: <><path d="m6 6 12 12M18 6 6 18"/></>,
    check: <><path d="m5 13 4 4L19 7"/></>,
    warning: <><path d="M12 3 2.8 20h18.4L12 3Z"/><path d="M12 9v4m0 3.5v.1"/></>,
    info: <><circle cx="12" cy="12" r="9"/><path d="M12 11v6m0-10v.1"/></>,
    plus: <><path d="M12 5v14M5 12h14"/></>,
    compare: <><path d="M8 5 4 9l4 4M4 9h14M16 19l4-4-4-4m4 4H6"/></>,
    coffee: <><path d="M5 8h11v6a5 5 0 0 1-5 5h-1a5 5 0 0 1-5-5V8Zm11 2h1.5a2.5 2.5 0 0 1 0 5H16M8 4c1 1 0 2 1 3m3-3c1 1 0 2 1 3"/></>,
    history: <><path d="M4 12a8 8 0 1 0 2.3-5.7L4 8.6M4 4v4.6h4.6M12 8v5l3 2"/></>,
    evidence: <><path d="M6 3h9l4 4v14H6V3Z M15 3v5h4M9 12h6m-6 4h6"/></>,
    system: <><circle cx="12" cy="12" r="3"/><path d="M12 3v3m0 12v3M3 12h3m12 0h3M5.6 5.6l2.1 2.1m8.6 8.6 2.1 2.1M18.4 5.6l-2.1 2.1m-8.6 8.6-2.1 2.1"/></>,
    /* Se rellenan en vez de trazarse: a 18 px un círculo de radio 1,4 dibujado
       con línea sale como un anillo y se lee como tres ojos. */
    more: <><circle cx="5" cy="12" r="1.5" fill="currentColor" stroke="none"/><circle cx="12" cy="12" r="1.5" fill="currentColor" stroke="none"/><circle cx="19" cy="12" r="1.5" fill="currentColor" stroke="none"/></>,
  }
  return <svg aria-hidden="true" width={size} height={size} viewBox="0 0 24 24" fill="none" stroke="currentColor" strokeWidth="1.45" strokeLinecap="round" strokeLinejoin="round">{paths[name]}</svg>
}

type ButtonProps = Omit<AriaButtonProps, 'children' | 'className'> & {
  children: ReactNode
  variant?: 'primary' | 'secondary' | 'quiet' | 'danger'
  icon?: IconName
  className?: string
}

export function Button({ variant = 'primary', icon, className = '', children, ...props }: ButtonProps) {
  return <AriaButton {...props} className={`${styles.button} ${styles[variant]} ${className}`}>
    <span>{children}</span>{icon ? <span className={styles.buttonIcon}><Icon name={icon} /></span> : null}
  </AriaButton>
}

export function IconButton({ label, icon, ...props }: Omit<ButtonProps, 'children' | 'icon'> & { label: string; icon: IconName }) {
  return <Button {...props} className={styles.iconButton} aria-label={label}><Icon name={icon} /></Button>
}

export function ButtonGroup({ children }: { children: ReactNode }) {
  return <div className={styles.buttonGroup}>{children}</div>
}

export function LinkButton({ to, children, icon, variant = 'primary' }: { to: string; children: ReactNode; icon?: IconName; variant?: ButtonProps['variant'] }) {
  return <Link to={to} className={`${styles.button} ${styles[variant]}`}>
    <span>{children}</span>{icon ? <span className={styles.buttonIcon}><Icon name={icon}/></span> : null}
  </Link>
}

interface FieldProps { label: string; description?: string; error?: string; children: ReactNode; id?: string }
export function Field({ label, description, error, children, id }: FieldProps) {
  return <label className={styles.field} htmlFor={id}>
    <span className={styles.label}>{label}</span>
    {description ? <span className={styles.description}>{description}</span> : null}
    {children}
    {error ? <span className={styles.error} role="alert">{error}</span> : null}
  </label>
}

export function Input({ className = '', ...props }: InputHTMLAttributes<HTMLInputElement>) {
  return <input {...props} className={`${styles.input} ${props['aria-invalid'] ? styles.invalid : ''} ${className}`} />
}

export function TextArea({ className = '', ...props }: TextareaHTMLAttributes<HTMLTextAreaElement>) {
  return <textarea {...props} className={`${styles.textarea} ${props['aria-invalid'] ? styles.invalid : ''} ${className}`} />
}

export function NumberField(props: InputHTMLAttributes<HTMLInputElement>) { return <Input {...props} type="number" inputMode="decimal" /> }
export function SearchField(props: InputHTMLAttributes<HTMLInputElement>) { return <Input {...props} type="search" /> }
export function DatePicker(props: InputHTMLAttributes<HTMLInputElement>) { return <Input {...props} type="date" /> }

export function DateRangePicker({ start, end, onStartChange, onEndChange }: { start: string; end: string; onStartChange: (value: string) => void; onEndChange: (value: string) => void }) {
  return <div className={styles.buttonGroup}>
    <Field label="Desde"><DatePicker value={start} onChange={(event) => onStartChange(event.target.value)} /></Field>
    <Field label="Hasta"><DatePicker value={end} onChange={(event) => onEndChange(event.target.value)} /></Field>
  </div>
}

export function Select(props: SelectHTMLAttributes<HTMLSelectElement>) {
  return <select {...props} className={styles.select}>{props.children}</select>
}

export function ComboBox({ label, options, value, onChange }: { label: string; options: string[]; value: string; onChange: (value: string) => void }) {
  const id = `combo-${label.toLowerCase().replaceAll(' ', '-')}`
  return <Field label={label} id={id}><Input id={id} list={`${id}-list`} value={value} onChange={(event) => onChange(event.target.value)} /><datalist id={`${id}-list`}>{options.map((option) => <option key={option}>{option}</option>)}</datalist></Field>
}

// Campo que busca entre opciones y, si se le pasa `onCreate`, ofrece crear lo
// escrito cuando no existe. La lista flota bajo el campo sin empujar el resto.
export function SearchSelect({ label, options, value, onChange, onCreate, createLabel, creating = false, placeholder }: {
  label: string
  options: Array<{ id: string; label: string }>
  value: string
  onChange: (id: string) => void
  onCreate?: (text: string) => void
  createLabel?: (text: string) => string
  creating?: boolean
  placeholder?: string
}) {
  const listId = useId()
  const [text, setText] = useState('')
  const [open, setOpen] = useState(false)
  const current = options.find((option) => option.id === value)
  const query = text.trim().toLocaleLowerCase('es')
  const matches = options.filter((option) => !query || option.label.toLocaleLowerCase('es').includes(query)).slice(0, 8)
  const exact = matches.some((option) => option.label.toLocaleLowerCase('es') === query)
  const choose = (id: string) => { onChange(id); setText(''); setOpen(false) }
  return <div className={styles.searchSelect}>
    <Field label={label}>
      <Input role="combobox" aria-expanded={open} aria-controls={listId} placeholder={placeholder} value={open ? text : current?.label ?? text}
        onFocus={() => { setText(''); setOpen(true) }} onBlur={() => window.setTimeout(() => setOpen(false), 150)} onChange={(event) => { setText(event.target.value); setOpen(true) }}/>
    </Field>
    {open ? <ul className={styles.searchSelectList} id={listId} role="listbox">
      {matches.map((option) => <li key={option.id} role="option" aria-selected={option.id === value}><button type="button" onMouseDown={(event) => event.preventDefault()} onClick={() => choose(option.id)}>{option.label}</button></li>)}
      {onCreate && query && !exact ? <li role="option" aria-selected={false}><button type="button" disabled={creating} onMouseDown={(event) => event.preventDefault()} onClick={() => onCreate(text.trim())}>{creating ? 'Creando…' : createLabel ? createLabel(text.trim()) : `Crear «${text.trim()}»`}</button></li> : null}
    </ul> : null}
  </div>
}

export function Checkbox({ label, ...props }: InputHTMLAttributes<HTMLInputElement> & { label: string }) {
  return <label className={styles.checkRow}><input {...props} type="checkbox" /><span>{label}</span></label>
}

export function RadioGroup({ label, name, options, value, onChange }: { label: string; name: string; options: Array<{ value: string; label: string }>; value: string; onChange: (value: string) => void }) {
  return <fieldset className={styles.field}><legend className={styles.label}>{label}</legend>{options.map((option) => <label className={styles.checkRow} key={option.value}><input type="radio" name={name} value={option.value} checked={value === option.value} onChange={() => onChange(option.value)} /><span>{option.label}</span></label>)}</fieldset>
}

export function Switch({ children, isSelected, onChange }: { children: ReactNode; isSelected: boolean; onChange: (value: boolean) => void }) {
  return <AriaSwitch className={styles.checkRow} isSelected={isSelected} onChange={onChange}><span className={styles.switch}/><span>{children}</span></AriaSwitch>
}

export function Surface({ children, className = '', ...props }: HTMLAttributes<HTMLDivElement>) {
  return <div {...props} className={`${styles.surfaceShell} ${className}`}><div className={styles.surface}>{children}</div></div>
}

// `wide` para lo que necesita espacio horizontal, como las curvas de una
// preparación: a 38rem las dos gráficas quedaban de un dedo de ancho.
export function Modal({ isOpen, onOpenChange, title, children, wide = false }: { isOpen: boolean; onOpenChange: (open: boolean) => void; title: string; children: ReactNode; wide?: boolean }) {
  return <ModalOverlay className={styles.modalOverlay} isOpen={isOpen} onOpenChange={onOpenChange} isDismissable>
    <AriaModal className={wide ? `${styles.modal} ${styles.modalWide}` : styles.modal}><Dialog aria-label={title}>
      <div className={styles.dialogHeader}><Heading slot="title" className={styles.dialogTitle}>{title}</Heading><IconButton label="Cerrar" icon="close" variant="quiet" onPress={() => onOpenChange(false)} /></div>
      {children}
    </Dialog></AriaModal>
  </ModalOverlay>
}

export function ConfirmDialog({ isOpen, onOpenChange, title, children, onConfirm }: { isOpen: boolean; onOpenChange: (open: boolean) => void; title: string; children: ReactNode; onConfirm: () => void }) {
  return <Modal isOpen={isOpen} onOpenChange={onOpenChange} title={title}>{children}<div className={styles.dialogActions}><Button variant="quiet" onPress={() => onOpenChange(false)}>Cancelar</Button><Button onPress={onConfirm}>Confirmar</Button></div></Modal>
}

export function Drawer(props: Parameters<typeof Modal>[0]) { return <Modal {...props} /> }

export function Popover({ trigger, children }: { trigger: ReactNode; children: ReactNode }) {
  return <TooltipTrigger delay={0}><span>{trigger}</span><AriaPopover className={styles.tooltip}>{children}</AriaPopover></TooltipTrigger>
}

export function Tooltip({ children, content }: { children: ReactNode; content: ReactNode }) {
  return <TooltipTrigger delay={300}><span>{children}</span><AriaTooltip className={styles.tooltip}>{content}</AriaTooltip></TooltipTrigger>
}
