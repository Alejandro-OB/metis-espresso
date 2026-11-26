# ADR-017 — Qué activa la configuración efectiva de la cafetera

**Estado:** Aceptada, 2026-09-21

## 1. Contexto

La plataforma propone, el barista decide y el dispositivo valida
([ADR-004](ADR-004-distribucion-datos-ontologia-aprendizaje.md) § 4.11). La aplicación
no puede iniciar preparaciones o escribir sobre los actuadores.
Esa cadena solo contemplaba aceptar una recomendación, pero ninguna procedencia
de recomendación está activa (ADR-004 § 11.4). Además, el barista suele elegir
el perfil del café sin que nadie le sugiera nada.

## 2. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Solo una recomendación aceptada | El ciclo no podría recorrerse ni cubriría el uso diario. Descartada. |
| Cualquier configuración marcada como efectiva | No constaría quién lo decidió. Descartada. |
| Tres vías con decisión humana y procedencia declarada | **Seleccionada.** |

## 4. Decisión

### 4.1 Vías de activación

Una configuración pasa a regir solo por selección (el barista elige una
existente), edición (la crea o modifica y la da por buena) o recomendación
aceptada (acepta o modifica una del sistema). Las tres producen una configuración
aceptada que baja por el canal de
[ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md). El
dispositivo la valida igual sea cual sea la vía y puede rechazarla con causa.
Ninguna vía inicia una preparación; eso sigue siendo del mando físico.

### 4.2 Procedencia

Toda configuración aceptada declara vía, autor y fecha y, si viene de una
recomendación, su identificador y el de la decisión que la aprobó. Así puede
reconstruirse por qué la máquina estaba ajustada de cierta forma.

### 4.3 Precedencia

Rige la última configuración aceptada. El historial conserva el orden y el
instante de cada aceptación.

### 4.4 Momento de entrada en vigor

Una configuración aceptada rige en cuanto la máquina no esté preparando. No entra
a mitad de una preparación porque el resumen guarda la configuración con que se
abrió la sesión, y el registro contradiría lo que siguió el lazo. No se exige la
máquina apagada, porque cada ajuste exigiría apagar y encender. La plataforma
ofrece la configuración vigente en cada intercambio, así que el dispositivo no
retiene nada; la que ya rige la reconoce por identificador y versión. La interfaz
debe decir que un cambio aceptado durante una preparación se aplica al café
siguiente.

### 4.5 Una configuración incompleta no se activa

Al activar, la plataforma exige referencia de temperatura, referencia de presión
y tiempo máximo, y nombra lo que falta. El dispositivo conserva su veto (§ 4.1),
pero el barista solo puede corregir mientras activa: en una prueba, una
configuración sin tiempo máximo fue aceptada por la plataforma y su rechazo quedó
solo en la consola del dispositivo. Los límites de la máquina los juzga el
dispositivo, no la plataforma.

### 4.6 Orden de implementación

Primero la selección, que permite recorrer el ciclo sin piezas ausentes; después
la edición y la recomendación aceptada. De los ajustes tras una preparación solo
derivan configuración los de temperatura, presión o tiempo máximo. La molienda,
la dosis y la proporción las ejecuta el barista, no la máquina.

## 5. Justificación

Las tres vías conservan una decisión humana registrada y el veto del
dispositivo. Lo que debe ser trazable es la procedencia de cada configuración, no
el número de vías.

## 6. Consecuencias

El ciclo puede verificarse sin esperar al razonador. La
plataforma pasa a generar configuraciones. Es más fácil cambiar la configuración
a mitad de una serie experimental, pero cada preparación conserva la
configuración vigente y su procedencia, y el cambio queda identificable.

## 8. Estado de implementación

La selección, la edición y la derivación desde un ajuste aceptado están
implementadas. Sobre el dispositivo real, al activar un tiempo máximo de 27 s
frente a los 30 s vigentes, la preparación siguiente terminó a los 26 998 ms por
límite de tiempo. Falta registrar el ciclo completo: quién propuso, quién aprobó,
qué validó el dispositivo y desde cuándo rige.

## 11. Criterios de reconsideración

Revisar si aparece una vía que no pueda declarar su procedencia o si el protocolo
experimental exige congelar la configuración durante una serie; entonces la
activación tendría que poder bloquearse mientras la serie esté abierta.
