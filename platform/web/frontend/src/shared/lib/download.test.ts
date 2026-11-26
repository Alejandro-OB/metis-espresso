import { afterEach, describe, expect, it, vi } from 'vitest'
import { downloadAttachment } from './download'

/* El defecto que motiva estas pruebas no se veía: los botones de descarga
   llamaban a `window.open`, el navegador cerraba la pestaña al recibir un
   adjunto —o el bloqueador de emergentes la descartaba— y el usuario pulsaba
   sin obtener nada ni ver un error. Ninguna prueba lo habría detectado, así
   que lo que se fija aquí es justamente lo que falló: que no se abra una
   pestaña y que el enlace esté en el documento cuando se pulsa, porque un
   enlace suelto fuera del árbol no descarga en todos los navegadores. */
describe('downloadAttachment', () => {
  afterEach(() => { vi.restoreAllMocks() })

  it('no abre una pestaña', () => {
    const abrir = vi.spyOn(window, 'open').mockReturnValue(null)
    vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(() => {})
    downloadAttachment('/api/v1/extractions/export?format=json')
    expect(abrir).not.toHaveBeenCalled()
  })

  it('pulsa un enlace al recurso, conectado al documento, y lo retira después', () => {
    const pulsaciones: Array<{ href: string; download: string | null; conectado: boolean }> = []
    vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(function (this: HTMLAnchorElement) {
      pulsaciones.push({ href: this.getAttribute('href') ?? '', download: this.getAttribute('download'), conectado: this.isConnected })
    })
    downloadAttachment('/api/v1/extractions/abc/export')
    expect(pulsaciones).toEqual([{ href: '/api/v1/extractions/abc/export', download: '', conectado: true }])
    expect(document.querySelectorAll('a[download]')).toHaveLength(0)
  })

  it('deja el nombre del archivo a la cabecera del servidor', () => {
    const nombres: Array<string | null> = []
    vi.spyOn(HTMLAnchorElement.prototype, 'click').mockImplementation(function (this: HTMLAnchorElement) {
      nombres.push(this.getAttribute('download'))
    })
    downloadAttachment('/api/v1/extractions/export?format=json')
    expect(nombres).toEqual([''])
  })
})
