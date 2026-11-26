# ADR-014 — Exposición de la plataforma externa en la red

**Estado:** Aceptada el 2026-09-21.

## 1. Contexto

La aplicación web de [ADR-003](ADR-003-interfaz-web-unica-en-plataforma-externa.md)
se ejecuta en la plataforma externa local. La plataforma tiene salida a
Internet para la hora, las actualizaciones y las imágenes de los servicios, pero
tener salida no implica ser alcanzable desde fuera. Solo la aplicación web
publica un puerto; la base de datos, el almacén RDF, la API y los trabajadores
solo son alcanzables dentro del despliegue. El tráfico no va cifrado. El
investigador puede necesitar consultar el historial fuera del laboratorio.

## 2. Alternativas consideradas

- **Publicar la aplicación en Internet con redirección de puertos.** Descartada:
  expone la instalación a todo el tráfico no solicitado y obliga a mantener
  certificados, contraseñas, parches y vigilancia sobre el equipo que guarda la
  evidencia experimental.
- **Publicarla con un intermediario en la nube.** Descartada: pone a un tercero
  en la ruta de los datos y crea la dependencia externa que
  [ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md) evita.
- **Mantenerla solo local, sin acceso remoto.** Descartada: deja sin resolver
  el acceso del investigador.
- **Mantenerla en la red local y dar acceso remoto con una red privada entre
  equipos.** Seleccionada.

## 3. Decisión

1. La aplicación no se publica en Internet: no se redirigen puertos ni se usan
   intermediarios.
2. La plataforma conserva su salida a Internet.
3. Solo se publica la aplicación web; los demás servicios siguen internos.
4. El investigador accede desde fuera por una red privada entre equipos, que no
   expone la aplicación y permite retirar el acceso de un equipo concreto.
5. Dentro de la instalación el tráfico circula sin cifrar, como limitación
   declarada del prototipo mientras la red sea acotada y de confianza. Las
   credenciales del dispositivo y de las personas son propias del sistema.

## 4. Razones

La necesidad real es el acceso del investigador, no la publicación, y una red
privada la cubre sin exponer nada. Exponer el equipo le traslada a un prototipo
las obligaciones de un servicio en producción. El tráfico sin cifrar es
asumible en una red acotada, no en una compartida, y declararlo permite saber
cuándo deja de serlo.

## 5. Consecuencias

- La instalación no recibe tráfico no solicitado de Internet ni necesita
  certificados.
- El acceso remoto se concede y se retira por equipo, y exige instalar la red
  privada en cada uno.
- Quien tenga acceso a la red local puede observar el tráfico, incluidas las
  credenciales; la del dispositivo es revocable
  ([ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md)).
- No se abren puertos ni siquiera para una demostración; tras cada cambio de
  red se comprueba que ningún puerto es alcanzable desde fuera.
- Si la instalación pasa a una red compartida con equipos ajenos, el tráfico se
  cifra antes de seguir operando.
