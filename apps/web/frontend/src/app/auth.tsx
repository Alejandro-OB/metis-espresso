import { useQuery, useQueryClient } from '@tanstack/react-query'
import { useCallback, useEffect, type ReactNode } from 'react'
import { api, post } from '@/shared/api/client'
import type { SessionUser } from '@/shared/api/types'
import { AuthContext } from './auth-context'

const sessionKey = ['session'] as const
const cachedUserKey = 'espresso-session-user'

function cachedUser(): SessionUser | undefined {
  try {
    const value = sessionStorage.getItem(cachedUserKey)
    return value ? JSON.parse(value) as SessionUser : undefined
  } catch {
    return undefined
  }
}

export function AuthProvider({ children }: { children: ReactNode }) {
  const queryClient = useQueryClient()
  const session = useQuery({
    queryKey: sessionKey,
    queryFn: () => api<SessionUser>('/api/v1/auth/session'),
    retry: false,
    staleTime: 60_000,
    initialData: cachedUser,
    initialDataUpdatedAt: 0,
  })

  useEffect(() => {
    const clearSession = () => {
      sessionStorage.removeItem(cachedUserKey)
      queryClient.setQueryData(sessionKey, null)
    }
    window.addEventListener('session-expired', clearSession)
    return () => window.removeEventListener('session-expired', clearSession)
  }, [queryClient])

  useEffect(() => {
    if (session.data) sessionStorage.setItem(cachedUserKey, JSON.stringify(session.data))
  }, [session.data])

  const login = useCallback(async (username: string, password: string) => {
    const user = await post<SessionUser>('/api/v1/auth/login', { username, password })
    sessionStorage.setItem(cachedUserKey, JSON.stringify(user))
    queryClient.setQueryData(sessionKey, user)
  }, [queryClient])

  const logout = useCallback(async () => {
    await post<void>('/api/v1/auth/logout', {})
    sessionStorage.removeItem(cachedUserKey)
    queryClient.setQueryData(sessionKey, null)
    queryClient.removeQueries({ predicate: (query) => query.queryKey[0] !== 'session' })
  }, [queryClient])

  return <AuthContext.Provider value={{
    user: session.data ?? null,
    isLoading: session.isLoading,
    login,
    logout,
  }}>{children}</AuthContext.Provider>
}
