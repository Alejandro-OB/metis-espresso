/* Descarga de un recurso que el servidor sirve como adjunto.

   Los tres botones de descarga usaban `window.open(url, '_blank')`. El
   navegador abre una pestaña, recibe un `Content-Disposition: attachment`, no
   tiene nada que mostrar y la cierra de inmediato; cuando además el gesto no
   llega marcado como interacción directa, el bloqueador de ventanas emergentes
   la descarta sin avisar. En los dos casos el usuario pulsa y no ocurre nada,
   ni siquiera un error en consola.

   Un enlace con `download` descarga en la propia página, sin abrir pestaña y
   sin depender del bloqueador. El atributo va vacío a propósito: así el nombre
   del archivo lo decide la cabecera del servidor, que ya lo envía
   (`extracciones-v1.json`), y no se duplica aquí. */
export function downloadAttachment(url: string): void {
  const link = document.createElement('a')
  link.href = url
  link.download = ''
  document.body.append(link)
  link.click()
  link.remove()
}
