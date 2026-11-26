import { createHash } from 'node:crypto'
import { readdir, writeFile } from 'node:fs/promises'
import { join, relative, sep } from 'node:path'

const dist = new URL('../dist/', import.meta.url)

async function files(directory) {
  const entries = await readdir(directory, { withFileTypes: true })
  const nested = await Promise.all(entries.map((entry) => {
    const path = join(directory.pathname, entry.name)
    return entry.isDirectory() ? files(new URL(`file://${path}/`)) : [path]
  }))
  return nested.flat()
}

const paths = (await files(dist))
  .map((path) => relative(dist.pathname, path).split(sep).join('/'))
  .filter((path) => path !== 'sw.js' && !path.endsWith('.map'))
  .map((path) => `/${path}`)
  .sort()

paths.unshift('/')
const version = createHash('sha256').update(paths.join('\n')).digest('hex').slice(0, 12)
const source = `const CACHE = 'aplicacion-web-shell-${version}'
const SHELL = ${JSON.stringify(paths, null, 2)}

self.addEventListener('install', (event) => {
  event.waitUntil(caches.open(CACHE).then((cache) => cache.addAll(SHELL)))
})

self.addEventListener('activate', (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((key) => key !== CACHE).map((key) => caches.delete(key))))
      .then(() => self.clients.claim()),
  )
})

self.addEventListener('fetch', (event) => {
  const request = event.request
  const url = new URL(request.url)
  if (request.method !== 'GET' || url.origin !== self.location.origin || url.pathname.startsWith('/api/')) return

  if (request.mode === 'navigate') {
    event.respondWith(fetch(request).catch(() => caches.match('/')))
    return
  }

  event.respondWith(
    caches.match(request).then((cached) => cached ?? fetch(request).then((response) => {
      if (response.ok) void caches.open(CACHE).then((cache) => cache.put(request, response.clone()))
      return response
    })),
  )
})
`

await writeFile(new URL('sw.js', dist), source)
