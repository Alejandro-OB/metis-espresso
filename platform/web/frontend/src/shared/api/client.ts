import type { ApiErrorShape } from './types'

export class ApiError extends Error {
  constructor(message: string, readonly code: string, readonly correlationId: string, readonly status: number) {
    super(message)
  }
}

export class PendingWriteError extends Error {}

interface PendingWrite { id: string; path: string; method: 'PUT'; payload: unknown }
const pendingKey = 'espresso:pending-writes'
export const pendingWritesEvent = 'espresso-pending-writes'

function pendingWrites(): PendingWrite[] {
  try { return JSON.parse(localStorage.getItem(pendingKey) ?? '[]') as PendingWrite[] } catch { return [] }
}

function savePending(writes: PendingWrite[]) {
  localStorage.setItem(pendingKey, JSON.stringify(writes))
  window.dispatchEvent(new CustomEvent(pendingWritesEvent, { detail: writes.length }))
}

export function pendingWriteCount() { return pendingWrites().length }

function enqueue(write: PendingWrite) {
  const writes = pendingWrites()
  if (!writes.some((item) => item.id === write.id)) savePending([...writes, write])
}

export async function api<T>(path: string, init?: RequestInit): Promise<T> {
  const headers = new Headers(init?.headers)
  headers.set('Accept', 'application/json')
  if (init?.body) headers.set('Content-Type', 'application/json')
  const response = await fetch(path, { ...init, headers, credentials: 'same-origin' })
  if (!response.ok) {
    let body: ApiErrorShape | undefined
    try { body = await response.json() as ApiErrorShape } catch { body = undefined }
    const error = new ApiError(
      body?.error.message ?? 'No fue posible completar la solicitud.',
      body?.error.code ?? 'network_error',
      body?.error.correlationId ?? response.headers.get('X-Correlation-ID') ?? 'sin-correlación',
      response.status,
    )
    if (response.status === 401 && path !== '/api/v1/auth/login') {
      window.dispatchEvent(new Event('session-expired'))
    }
    throw error
  }
  if (response.status === 204) return undefined as T
  return response.json() as Promise<T>
}

export function post<T>(path: string, payload: unknown): Promise<T> {
  return api<T>(path, { method: 'POST', body: JSON.stringify(payload) })
}

export function put<T>(path: string, payload: unknown): Promise<T> {
  return api<T>(path, { method: 'PUT', body: JSON.stringify(payload) })
}

export async function resilientPut<T>(path: string, payload: unknown): Promise<T> {
  const id = crypto.randomUUID()
  try {
    return await api<T>(path, { method: 'PUT', body: JSON.stringify(payload), headers: { 'Idempotency-Key': id } })
  } catch (error) {
    if (!(error instanceof ApiError) || error.status >= 500) {
      enqueue({ id, path, method: 'PUT', payload })
      throw new PendingWriteError('Guardamos esta acción en el dispositivo. Se enviará cuando vuelva la conexión.')
    }
    throw error
  }
}

export async function flushPendingWrites(): Promise<number> {
  const writes = pendingWrites()
  const remaining: PendingWrite[] = []
  let sent = 0
  for (const write of writes) {
    try {
      await api(write.path, { method: write.method, body: JSON.stringify(write.payload), headers: { 'Idempotency-Key': write.id } })
      sent += 1
    } catch (error) {
      if (!(error instanceof ApiError) || error.status >= 500) remaining.push(write)
    }
  }
  savePending(remaining)
  return sent
}
