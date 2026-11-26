# ADR-005 — Regulación permanente, autoridad de interacción y seguridad operacional

**Estado:** Aceptada, 2026-08-23. La § 12 se añadió el 2026-09-30.

## 1. Contexto

La temperatura y la presión deben regularse siempre, sin que la cafetera
inicie preparaciones ni cambie referencias por su cuenta. El prototipo no
comprueba la presencia del barista ni detecta falta de agua, y la norma
IEC 60335-2-15:2024 trata la operación remota como función de seguridad.

## 3. Alternativas consideradas

| Alternativa | Razón |
|---|---|
| Regulación habilitada por un «modo de control» | Contradice la estabilidad permanente y crea estados ambiguos. Descartada. |
| Inicio desde la web o una interfaz autenticada | No confirma que la máquina esté preparada. Descartada. |
| Preparación autónoma por receta | Quita la autoridad al barista. Descartada. |
| Inicio y parada normal con el mando físico; desconexión del enchufe para intervenir | Exige la presencia del barista. **Seleccionada** para el prototipo supervisado. |

## 4. Decisión

### 4.1 Regulación automática sin decisión autónoma

No hay modo de control seleccionable. En calentamiento y preparada se regula la
temperatura; en extracción activa, temperatura y presión; en estado seguro,
inactivo o alarma, los actuadores quedan en la condición segura. La máquina de
estados del firmware es la autoridad operativa. Los controladores reducen el
error respecto de una referencia autorizada, pero no pueden modificarla,
iniciar una extracción, cambiar el criterio de terminación ni eludir una
protección.

### 4.2 Autoridad de las interfaces

| Medio o componente | Iniciar extracción | Parada normal anticipada | Desconexión para intervenir |
|---|---:|---:|---:|
| Mando físico de la cafetera | Sí | Sí | No |
| Retirar el enchufe | No | No | Sí |
| Aplicación web, servicios externos, ontología, razonador o modelo | No | No | No |
| Supervisor de seguridad | No | No; termina por protección | No |

La máquina de estados comprueba que el sistema está preparado antes de activar
la bomba. Ningún temporizador, perfil, interfaz, modelo ni reconexión puede
iniciar una extracción.

El prototipo no tiene parada de emergencia, contactor `K_SAFE` ni relé
intermedio, y el botón del panel solo cambia el estado lógico. Por eso opera
supervisado y se desconecta del enchufe antes de intervenirlo.

### 4.3 Criterios, causas y prioridad de terminación

Una extracción termina por una de estas causas:

1. `maximum-extraction-time-reached`: se alcanza el tiempo máximo configurado;
2. `manual-normal-stop`: el barista la detiene con el mando físico;
3. `safety-protection-trip`: actúa una protección o el supervisor de seguridad.

La precedencia es `safety-protection-trip` → `manual-normal-stop` →
`maximum-extraction-time-reached`. Se registra la primera causa de mayor
prioridad que acepta la máquina de estados, nunca una inferida de los valores
finales. La masa, el TDS y las recomendaciones no terminan una extracción.

Retirar el enchufe no es una cuarta causa, porque puede cortar la energía
antes de escribirla. Al abrir una preparación, el firmware guarda en NVS una
marca (`open_extraction.h`) y al cerrarla la borra. El arranque que la
encuentra envía el registro como `unexpected-power-loss`, con `finishedAt` y
`durationMs` nulos y cero muestras, porque no consta cuándo se cortó la
energía. Se descartó reescribir la marca durante la preparación: cada
escritura en flash detiene la caché mientras regula el lazo de presión. El
envío depende de `CAFE_REPORT_INTERRUPTED_EXTRACTIONS`: una plataforma que no
admita esos nulos rechazaría el lote entero.

### 4.4 Perfiles y configuración efectiva

Elegir un perfil es opcional. Una configuración inválida no se activa y sigue
vigente la última válida. La falta de perfil o de servidor no bloquea una
extracción.

### 4.5 Recomendaciones y cambios de referencia

El recorrido autorizado de una recomendación es:

```text
recomendación
    → decisión explícita del barista
    → configuración pendiente
    → validación local
    → activación explícita
    → inicio físico de una extracción posterior
    → evaluación del resultado
```

Aceptar una recomendación no la activa, y activarla no inicia una extracción.

### 4.6 Prioridad de seguridad

Las protecciones tienen prioridad sobre mandos, interfaz, regulación y
recomendaciones.

## 5. Consecuencias

El barista debe estar junto a la cafetera para iniciar. La solución sirve solo
para un prototipo supervisado y no equivale a una parada de emergencia
certificada.

## 8. Verificación de la decisión

Se comprueba como mínimo:

1. regulación térmica en calentamiento, preparada y extracción;
2. regulación de presión solo durante la extracción;
3. rechazo del inicio desde la web, una API, una recomendación, una reconexión
   o un reinicio;
4. inicio físico solo en estado preparado;
5. terminación por tiempo máximo;
6. parada normal con el mando físico y su origen registrado;
7. ingreso manual validado de dosis, masa final y TDS;
8. que esos valores no detienen la bomba ni cambian referencias;
9. pérdida del wifi sin afectar las paradas ni el tiempo máximo;
10. desconexión del enchufe y recuperación del registro incompleto al volver a
    energizar;
11. actuación de las protecciones ante sensores inválidos;
12. precedencia correcta cuando coinciden dos causas;
13. rechazo de configuraciones inválidas sin alterar la efectiva;
14. que el razonador, el modelo o el servidor no escriben actuadores ni
    referencias activas;
15. recuperación controlada tras cada terminación o alarma.

## 9. Trazabilidad y referencias

[IEC 60335-2-15:2024](https://webstore.iec.ch/en/publication/70372);
[NIST, *Logical Access to Interfaces*](https://pages.nist.gov/IoT-Device-Cybersecurity-Requirement-Catalogs/technical/logical/).

## 11. Criterios de reconsideración

El inicio desde una interfaz exige un nuevo alcance, análisis formal de
riesgos, autenticación y confirmación local. Las fases automáticas, el cambio
autónomo de referencias y la aplicación automática de recomendaciones exigen
nuevos requisitos, autorización académica y validación. Ningún servicio externo
accede directamente a los actuadores.

## 12. Cambio de referencias para sostener el objetivo

[ADR-018](ADR-018-politica-de-sostenimiento-del-objetivo.md) permite que la
plataforma cambie referencias entre preparaciones para sostener el objetivo que
el barista activó. La corrección ocurre fuera del lazo y llega como
configuración que el dispositivo valida. En el recorrido de la § 4.5, la
decisión explícita es la activación del objetivo.

### 12.2 Condiciones de la § 11

| Condición | Estado |
|---|---|
| Requisito del sostenimiento del objetivo | Añadido |
| Autorización académica | Concedida el 2026-09-30 por los autores al aprobar ADR-018 |
| Validación | Definida en ADR-018 § 9 |

Su activación depende además de la puesta en servicio que exige el guardián.
