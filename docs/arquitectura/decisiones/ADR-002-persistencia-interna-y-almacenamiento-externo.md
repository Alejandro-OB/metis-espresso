# ADR-002 — Persistencia interna mínima y almacenamiento histórico externo asíncrono

**Estado:** Aceptada, revisada; capacidad local verificada
**Fecha de decisión original:** 2026-08-23  
**Fecha de revisión:** 2026-09-09
**Ámbito:** persistencia local, recuperación, historial y sincronización  
**Reemplaza:** la decisión inicial del mismo ADR que exigía una microSD externa como almacenamiento operativo principal

## 1. Contexto

La cafetera debe iniciar, regular y finalizar una extracción aunque no exista
Internet ni un servidor disponible. La información indispensable para esa
operación —configuración efectiva, límites de seguridad, calibraciones, estado
de recuperación e identidad del dispositivo— tiene un volumen pequeño y debe
permanecer en la memoria no volátil de la ESP32-S3. La tarjeta ESP32-S3-N16R8
dispone de 16 MB de flash y 8 MB de PSRAM, capacidades confirmadas mediante
el firmware de diagnóstico.

Las series temporales, el historial completo, las evaluaciones, los análisis,
la representación semántica y los artefactos de aprendizaje aportan valor al
sistema, pero no son precondiciones para ejecutar una extracción. Mantener
indefinidamente esos datos en una microSD agrega ranura, bus, componentes,
tratamiento de retiro y modos de fallo que no son necesarios para el prototipo
mínimo.

La flash interna no se utilizará como un historial ilimitado. Su función será
conservar el estado operativo y una cola acotada que permita tolerar
desconexiones temporales del servicio externo.

## 2. Problema de decisión

Se debe establecer qué información necesita persistencia local para operar con
seguridad, cómo se conservan temporalmente las preparaciones mientras el
servidor no está disponible y dónde reside el historial completo sin convertir
la disponibilidad de red en una dependencia del control.

## 3. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| microSD como almacén principal | Ofrece gran retención sin servidor, pero agrega hardware, buses, mantenimiento y fallos de medio removible que no son indispensables para ejecutar una extracción. |
| Servidor sin persistencia local | Simplifica el dispositivo, pero una desconexión perdería preparaciones y no permitiría una sincronización diferida fiable. |
| Flash interna con cola acotada y servicio histórico externo | Conserva autonomía operativa, tolera desconexiones temporales y concentra el historial y el análisis en la infraestructura apropiada. Alternativa seleccionada. |
| Flash externa soldada | Amplía la retención local sin un medio removible. Se conserva como alternativa futura si las mediciones muestran que la flash del módulo es insuficiente. |

## 4. Decisión

La primera versión no incorporará una microSD obligatoria. Se utilizará la
ESP32-S3-N16R8 confirmada, con 16 MB de flash y 8 MB de PSRAM. La tabla actual
reserva 256 KiB para NVS y una partición de aplicación de 3 MiB; no incluye
particiones OTA. La capacidad total de flash no se considera disponible para la
cola, porque también debe alojar el firmware y las particiones definidas.

La distribución será la siguiente:

| Ubicación | Contenido autorizado |
|---|---|
| RAM o PSRAM | Muestras, estados y buffers de la extracción en curso. Su contenido es temporal. |
| NVS de la ESP32-S3 | Identidad del dispositivo, credenciales, configuración mínima, límites protegidos, calibraciones, perfil o configuración efectiva, contadores y estado de recuperación. |
| Flash interna de la ESP32-S3 | Firmware, configuración local y cola limitada de preparaciones pendientes. La tabla actual reserva 256 KiB para NVS y 3 MiB para la aplicación. |
| Plataforma externa local — candidata: Raspberry Pi | Historial completo, series temporales, eventos, alarmas, indicadores, evaluaciones, datos experimentales, copias de respaldo, ontología, razonamiento, modelos y resultados de simulación o aprendizaje, cuando el despliegue sea validado. |

La capacidad actualmente verificada para la cola NVS es de 32 sobres de hasta
1536 bytes cada uno. La prueba en la tarjeta física confirmó escritura, lectura
en orden, deduplicación y eliminación selectiva mediante confirmación (`ACK`);
al terminar el autodiagnóstico, la cola quedó vacía (`0/32`). Esta capacidad
define el límite operativo conocido del prototipo y deberá revisarse si cambia
el formato de los mensajes o la tabla de particiones.

La plataforma externa podrá ejecutarse inicialmente en una Raspberry Pi. El
contrato de arquitectura será independiente del proveedor y de la tecnología
de almacenamiento. La base de datos conservará identidades, metadatos,
indicadores y relaciones; las series de mayor volumen podrán almacenarse como
objetos referenciados desde el registro de la preparación.

Cada preparación tendrá un identificador estable antes de iniciarse. Como
mínimo se distinguirán los estados `en curso`, `pendiente de sincronización`,
`sincronizada`, `incompleta` y `error de persistencia`. La confirmación del
servidor será idempotente: reenviar el mismo identificador no creará una
preparación duplicada.

Durante una extracción las muestras se mantendrán en buffers que no bloqueen
las tareas críticas. Al finalizar, el sistema cerrará el registro y lo
incorporará por lotes a la cola local. No se escribirán muestras de alta
frecuencia como pares individuales en NVS.

El registro cerrado conservará la configuración aplicada, la causa real
aceptada por la máquina de estados, el tiempo de finalización, las series
técnicas y los eventos. La dosis, la masa final de bebida y el TDS podrán
incorporarse después como entradas manuales con unidad, autor y procedencia;
el rendimiento se calculará únicamente cuando existan las tres entradas. La
ausencia de esos datos no impide cerrar ni sincronizar la preparación y ninguno
de ellos participa en el control.

Cuando el servicio externo no esté disponible, la cafetera continuará
regulando y ejecutando extracciones. Los registros completos permanecerán
localmente hasta que puedan sincronizarse o hasta alcanzar el límite de
retención definido. La interfaz mostrará el estado de sincronización.

Si la cola se llena o la partición local falla, el sistema conservará el
control y la seguridad, advertirá que no puede garantizar el registro histórico
completo y no presentará la preparación como evidencia experimental completa.
Esta degradación no impedirá una extracción ordinaria. Las condiciones de
seguridad y las mediciones necesarias para regular permanecen independientes
del almacenamiento y del servidor.

Después de confirmar la recepción externa, la política de retención podrá
eliminar las series locales más antiguas y conservar únicamente resúmenes o los
registros recientes definidos por el presupuesto de memoria.

Las credenciales administrativas del servicio externo no se almacenarán en la
ESP32-S3. La sincronización utilizará una API de ingestión con identidad y
permisos limitados por dispositivo; esa API validará el esquema antes de
persistir los datos.

## 5. Consecuencias

### 5.1 Consecuencias favorables

- Se elimina la ranura microSD, su interfaz eléctrica y sus fallos de retiro o contacto.
- La extracción continúa sin Internet ni servidor.
- La flash se utiliza como mecanismo acotado de tolerancia a desconexiones, no como historial ilimitado.
- El historial, las comparaciones, la ontología y el aprendizaje se ejecutan donde existen recursos apropiados.
- La sincronización diferida permite reintentos y deduplicación.
- Una ampliación futura de almacenamiento no modifica los lazos de control.

### 5.2 Consecuencias desfavorables

- La cantidad de preparaciones completas disponibles localmente será limitada.
- Una desconexión prolongada puede agotar la cola y provocar pérdida de nuevas series históricas.
- Se requiere dimensionar con cuidado la tabla de particiones y el desgaste de la flash.
- El historial completo y las comparaciones de mayor alcance dependerán de un servicio externo disponible de forma periódica.
- La sincronización, la autenticación y la independencia del proveedor aumentan la complejidad del software externo.

## 6. Riesgos y medidas de tratamiento

| Riesgo | Tratamiento requerido |
|---|---|
| Desgaste de la flash | Escrituras por lotes, sistema de archivos con nivelación de desgaste, cola circular y pruebas de vida útil. |
| Interrupción durante la persistencia | Registro transaccional o diario, marcas de preparación incompleta y recuperación al reiniciar. |
| Servidor desconectado por largo tiempo | Cola acotada, indicador de ocupación, advertencias y política explícita cuando se alcance el límite. |
| Duplicación durante reintentos | Identificador estable, operación idempotente y confirmación externa antes de liberar el registro local. |
| Pérdida de la plataforma externa o de la cuenta | Formatos exportables, copias de respaldo y contrato independiente del proveedor. |
| Credencial privilegiada expuesta | API de ingestión con credencial limitada por dispositivo; ninguna clave administrativa en firmware o interfaz. |
| Interferencia con el control | Persistencia y sincronización en tareas no críticas mediante colas limitadas y sin acceso a actuadores. |

## 7. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Frecuencias, estructura y tamaño de muestra | Medir el volumen real de una preparación y el peor caso de una sesión. |
| Tabla de particiones | Documentar el uso de la tabla vigente y decidir si se requiere OTA o una partición de datos adicional antes de ampliar el firmware. |
| Sistema de archivos y formato local | Seleccionar mediante pruebas de desgaste, recuperación, rendimiento y corte de energía. |
| Capacidad y política de la cola | Capacidad base verificada: 32 sobres de hasta 1536 bytes en la partición NVS de 256 KiB. Falta definir la retención después de sincronizar y documentar el comportamiento operativo al llenarse. |
| Plataforma externa y esquema | Validar la Raspberry Pi y definir base de datos, almacenamiento de objetos, identificadores, API, autenticación y copias de respaldo en la arquitectura de datos. |
| Política para campañas experimentales | Determinar cuándo la disponibilidad de registro completo será una precondición del protocolo experimental, sin convertirla en precondición de una extracción ordinaria. |

## 8. Verificación de la decisión

Se probarán, como mínimo:

1. extracción y control con servidor e Internet desconectados;
2. cierre local, reinicio y recuperación de una preparación pendiente;
3. sincronización repetida sin duplicación;
4. interrupción de energía durante la persistencia;
5. cola llena y error de escritura sin pérdida de supervisión de actuadores;
6. estrés simultáneo de adquisición, regulación, web, persistencia y sincronización;
7. exportación y reconstrucción de indicadores desde los datos del servidor;
8. imposibilidad de que una reconexión o respuesta del servidor inicie una extracción o modifique una referencia activa.

La decisión se considerará verificada cuando las tareas críticas cumplan sus
plazos, los registros pendientes se recuperen de forma inequívoca, los reintentos
no dupliquen preparaciones y el sistema informe correctamente toda pérdida de
capacidad histórica.

## 9. Trazabilidad

- Requisitos funcionales: RF-014–RF-016 y RF-027.
- Requisitos no funcionales: RNF-001, RNF-006 y RNF-007.
- Restricciones de diseño: RD-001, RD-003 y RD-005.

## 10. Criterios de reconsideración

La decisión deberá revisarse si el protocolo exige conservar localmente series
completas durante periodos mayores que la cola dimensionada, si la conectividad
real impide sincronizar con la frecuencia necesaria, si la flash disponible no
ofrece capacidad o vida útil suficiente o si las pruebas muestran que una
memoria externa soldada o una microSD aporta una ventaja demostrable. Cualquier
ampliación de almacenamiento seguirá fuera del lazo de control.
