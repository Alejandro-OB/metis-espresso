# ADR-015 — Medio de almacenamiento, retención y respaldo de la plataforma externa

**Estado:** Aceptada (2026-09-21)

## 1. Contexto

La plataforma externa conserva el historial de preparaciones, las series
medidas, las simulaciones, el grafo semántico y la auditoría. ADR-002 sitúa el
historial fuera del dispositivo, así que perder el almacenamiento de la
plataforma es perder la evidencia experimental. Una
interrupción no debe dañar los registros confirmados.

La plataforma es una Raspberry Pi que arranca desde una tarjeta de memoria de
16 GB (14,3 GB útiles). También se dispone de un disco externo Toshiba
MK5075GSX de 500 GB por USB, de 2012, con 216 sectores reasignados: funciona,
pero ya perdió superficie.

Una preparación completa —resumen, serie a una muestra por segundo, simulación
y hechos semánticos— ocupa del orden de 45 KB. Mil preparaciones suman unos
50 MB. El espacio no es la restricción; lo es la fiabilidad del medio.

El montaje es el prototipo de un trabajo de grado, con un horizonte de meses y
bajo supervisión. A esa escala, el desgaste de una tarjeta por escritura no es
un argumento de peso.

## 3. Alternativas descartadas

| Alternativa | Razón |
|---|---|
| Sistema en la tarjeta y contenedores en el disco | El disco es el componente frágil: tiene sectores reasignados y se desconectó durante un borrado intensivo. Además, la versión instalada del motor de contenedores ignora `data-root` para las imágenes y las escribe igual en la tarjeta. |
| Todo en el disco, arranque incluido | Concentra sistema y datos en el componente más antiguo y exige arrancar por USB. |
| Comprar una tarjeta de 32 GB | El margen se consiguió sin gastar (§ 4.1). Se reconsidera si la ocupación supera el 80 %. |
| Podar series antiguas | No libera nada relevante y destruye evidencia irrepetible: cada preparación es una prueba con su ajuste, su medición y su evaluación sensorial. |

## 4. Decisión

1. **El sistema y la plataforma completa residen en la tarjeta de 16 GB.** Para
   que quepan se retiró el archivo de intercambio de fábrica (2,0 GB; queda el
   intercambio en memoria comprimida, porque el equipo tiene 8 GiB y usa 0,4),
   se borraron las cachés de instalación (0,4 GB) y se limitó la rotación de los
   registros de los contenedores. La ocupación sin datos baja del 52 % al 35 %
   (4,6 GB) y llega al 54 % con dos juegos de imágenes durante una
   actualización reversible (ADR-016).
2. **Se conserva todo el historial**, sin poda.
3. **Se hace una copia diaria a un medio distinto del principal, y se comprueba
   restaurándola.** Una copia que nunca se ha restaurado no acredita nada. La
   tarjeta puede fallar sin avisar, y la protección contra eso es la copia, no
   otro medio. La copia la hace [`respaldar.sh`](../../../platform/web/respaldar.sh),
   disparada a las 03:00 por
   [`espresso-respaldo.timer`](../../../platform/web/systemd/espresso-respaldo.timer),
   que recupera la copia perdida si el equipo estaba apagado;
   [`restaurar.sh`](../../../platform/web/restaurar.sh) la restaura.
4. **El disco externo es destino de copia, no almacén de operación.** Si falla
   una copia, se detecta al verificarla y se repone; si fallara el almacén
   primario, se perdería el historial. Con el reparto de corriente ampliado, el
   disco soportó tres ciclos de 20 000 archivos sin desconectarse.
5. **Los servicios no dependen del disco para arrancar.** Sin el disco solo se
   pierde la copia, y esa condición debe ser visible.
6. **El estado del disco se vigila.** Si aparecen sectores pendientes o crecen
   los reasignados, se retira también como destino de copia.
7. **Mientras el disco esté conectado, el reparto de corriente de sus puertos
   sigue ampliado**, porque el de fábrica no cubre su consumo en ráfaga y ya
   provocó una desconexión en caliente.
8. **La ocupación de la tarjeta se registra en cada medición.** Por encima del
   80 % se sustituye por una mayor antes de continuar.

## 6. Consecuencias

- La operación se apoya en un medio que falla sin avisar. Toda la protección
  recae en la copia diaria, que pasa a ser obligatoria.
- El margen depende de las medidas de § 4.1. Si se restablece el archivo de
  intercambio o se deja crecer los registros sin límite, la decisión pierde su
  fundamento.
- El desgaste de la tarjeta no es observable, por lo que la decisión queda atada
  al horizonte de meses. Si el sistema pasa a operación permanente, se reabre, y
  la alternativa sería un disco de estado sólido.

## 9. Verificación de la decisión

1. Tras un corte de energía, los servicios arrancan y operan sin ningún medio
   externo.
2. La copia diaria se genera y se restaura en un entorno separado. Comprobado el
   2026-10-30: la copia tardó 34 s y ocupó 138 MB, con sumas verificadas; la
   restauración en otro equipo reprodujo las mismas respuestas de la API.
3. La ocupación de la tarjeta queda por debajo del 80 % en cada medición.
4. La escritura acumulada en operación se mide y se contrasta con el horizonte
   declarado.
5. El disco no presenta sectores pendientes.

## 10. Trazabilidad

- [ADR-002](ADR-002-persistencia-interna-y-almacenamiento-externo.md): el
  historial reside fuera del dispositivo.
- [ADR-010](ADR-010-residencia-y-memoria-del-almacen-rdf.md) y
  [ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md):
  servicios y datos que la plataforma conserva.

## 11. Criterios de reconsideración

Se revisa si el volumen de datos crece un orden de magnitud, si el disco
presenta deterioro activo o si la instalación pasa a requerir disponibilidad
continua, caso en que haría falta redundancia en línea.
