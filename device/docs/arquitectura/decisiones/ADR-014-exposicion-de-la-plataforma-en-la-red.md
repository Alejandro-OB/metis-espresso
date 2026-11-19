# ADR-014 — Exposición de la plataforma externa en la red

**Estado:** Aceptada
**Fecha de redacción:** 2026-09-15
**Fecha de aprobación:** 2026-09-21
**Ámbito:** alcance de red de la aplicación y sus servicios, acceso remoto y protección del tráfico

## 1. Contexto

ADR-003 establece que la aplicación web es la única interfaz del sistema y que
se ejecuta en la plataforma externa local. La plataforma dispone normalmente de
salida a Internet, que utiliza para obtener la hora, actualizaciones e imágenes
de los servicios. Disponer de salida no implica ser alcanzable desde fuera:
son dos cosas distintas y conviene decidirlas por separado.

El investigador puede necesitar consultar el historial sin estar en el
laboratorio. Esa necesidad es la que empuja a publicar la aplicación en
Internet, y es la que esta decisión debe resolver sin asumir el coste de
exponerla.

### 1.1 Situación comprobada

Comprobado el 2026-09-15 sobre el despliegue:

| Elemento | Situación |
|---|---|
| Servicios publicados fuera del entorno | **Únicamente la aplicación web**, en un puerto |
| Base de datos, almacén RDF, API y trabajadores | No publican puerto: solo alcanzables desde la red interna del despliegue |
| Cifrado del tráfico | **Ninguno**: la aplicación se sirve sin cifrar |
| Origen cruzado | Restringido: no se admiten orígenes externos |
| Credenciales de prueba | Aceptadas solo en entornos de desarrollo y prueba |
| Enlace del dispositivo | Credencial en cabecera, sobre tráfico sin cifrar |

La superficie expuesta es, por tanto, mínima y el problema no es cuántos
servicios se publican, sino **a qué red quedan asomados**.

## 2. Problema de decisión

Se debe definir desde dónde es alcanzable la aplicación, cómo se resuelve el
acceso del investigador fuera del laboratorio y qué protección se exige al
tráfico dentro de la instalación.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Publicar la aplicación en Internet mediante redirección de puertos | Resuelve el acceso remoto y expone la instalación a todo el tráfico no solicitado de Internet. Obligaría a mantener cifrado con certificados renovables, política de contraseñas, actualizaciones de seguridad y vigilancia de accesos, sobre un equipo que también sostiene la evidencia experimental. |
| Publicarla mediante un servicio intermediario en la nube | Evita abrir puertos, pero introduce un tercero en la ruta de datos experimentales y una dependencia externa que ADR-001 quiso evitar. |
| Mantenerla accesible solo en la red local y resolver el acceso remoto con una red privada entre equipos | Conserva la superficie actual, no publica nada en Internet y permite al investigador conectarse desde fuera como si estuviera en la red local. **Seleccionada.** |
| Mantenerla solo local sin acceso remoto | Es lo más simple, pero deja sin resolver una necesidad real del trabajo experimental. |

## 4. Decisión

1. **La aplicación no se publicará en Internet.** No se redirigirán puertos hacia
   la plataforma ni se utilizarán intermediarios que la hagan alcanzable desde
   fuera.
2. **La plataforma conserva su salida a Internet** para hora, actualizaciones y
   descarga de imágenes. Salida y exposición son independientes.
3. **Solo se publica la aplicación web.** La base de datos, el almacén RDF, la
   API y los trabajadores permanecen accesibles únicamente dentro del entorno de
   despliegue, como ya ocurre.
4. **El acceso remoto del investigador se resolverá mediante una red privada
   entre equipos**, que no expone la aplicación y permite retirar el acceso de un
   equipo concreto sin afectar a los demás.
5. **Dentro de la instalación el tráfico circula sin cifrar**, lo que se acepta
   como limitación declarada del prototipo mientras la red sea acotada y de
   confianza. Las credenciales del dispositivo y de las personas usuarias serán
   propias del sistema y no reutilizadas de otros servicios.

## 5. Justificación

1. **La necesidad real es el acceso del investigador, no la publicación.** Una
   red privada la cubre sin exponer nada.
2. **Exponer traslada a un prototipo obligaciones de un servicio en producción**:
   certificados, actualizaciones de seguridad, vigilancia y respuesta ante
   accesos indebidos, sobre el mismo equipo que conserva la evidencia
   experimental.
3. **La superficie ya es mínima y conviene conservarla así.** Solo la aplicación
   se publica; todo lo demás permanece interno.
4. **El tráfico sin cifrar es asumible en una red acotada y no lo es en una red
   compartida.** Declararlo permite decidir cuándo deja de serlo, en lugar de
   descubrirlo tarde.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- La instalación no recibe tráfico no solicitado de Internet.
- El acceso remoto se concede y se retira por equipo.
- No se introducen terceros en la ruta de los datos experimentales.
- No hacen falta certificados ni renovaciones para operar.

### 6.2 Consecuencias desfavorables

- El acceso remoto exige instalar y mantener la red privada en cada equipo desde
  el que se consulte.
- Sin cifrado, quien tenga acceso a la red local puede observar el tráfico,
  incluidas las credenciales.
- Mostrar el sistema a alguien externo requiere darle acceso a la red privada o
  hacerlo presencialmente.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| La instalación acaba en una red compartida y amplia | Condición de reconsideración explícita en § 9: con red compartida, cifrar el tráfico y endurecer las credenciales antes de operar. |
| Apertura de puertos «temporal» para una demostración | Prohibida por esta decisión; una demostración remota se hace por la red privada. |
| Credencial del dispositivo observable en la red local | Credencial propia y revocable, según ADR-012; red acotada. |
| Publicación accidental al cambiar de router o de red | Comprobar, tras cada cambio de red, que ningún puerto de la plataforma es alcanzable desde fuera. |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Red privada para el acceso remoto | Instalarla en la plataforma y en los equipos del investigador, y comprobar el acceso desde fuera del laboratorio. |
| Comprobación de no exposición | Verificar desde fuera de la red local que la plataforma no es alcanzable. |
| Contraseñas de la instalación | Sustituir las credenciales de desarrollo por credenciales propias antes de operar. |

## 9. Criterios de reconsideración

La decisión deberá revisarse si la instalación pasa a una red compartida con
equipos ajenos —en cuyo caso el tráfico deberá cifrarse antes de seguir
operando—, si aparece la necesidad de que personas ajenas al proyecto consulten
la aplicación de forma habitual, o si el sistema deja de ser un prototipo de
laboratorio.

## 10. Trazabilidad

- [ADR-001](ADR-001-arquitectura-esp32s3-plataforma-externa.md): plataforma local
  y autonomía respecto de servicios en la nube.
- [ADR-003](ADR-003-interfaz-web-unica-en-plataforma-externa.md): interfaz única
  y frontera de comunicación.
- [ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md):
  identidad y credencial del dispositivo.
- Alcance de producción de la plataforma externa.
- Requisitos: RF-013 y RNF-001.
