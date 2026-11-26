# ADR-015 — Medio de almacenamiento, retención y respaldo de la plataforma externa

**Estado:** Aceptada, revisada sobre medidas tomadas en el equipo
**Fecha de redacción:** 2026-09-15
**Fecha de aprobación:** 2026-09-21
**Fecha de revisión:** 2026-09-15
**Reemplaza:** la primera redacción de este ADR, que situaba la plataforma de
contenedores en el disco externo. La revisión se explica en § 3.3
**Ámbito:** soporte físico donde residen los datos de la plataforma externa, política de retención y estrategia de respaldo

## 1. Contexto

La plataforma externa conserva el historial de preparaciones, las series
medidas, los resultados de simulación, el grafo semántico y la auditoría. RNF-006
exige que una interrupción no dañe los registros confirmados. ADR-002 sitúa
deliberadamente el historial fuera del dispositivo, de modo que la pérdida del
almacenamiento de la plataforma es la pérdida de la evidencia experimental.

La plataforma externa es una Raspberry Pi, que carece de almacenamiento propio y
arranca desde una tarjeta de memoria.

### 1.1 Volumen real de los datos

Medido el 2026-09-15 sobre el despliegue en el entorno de desarrollo:

| Elemento | Tamaño |
|---|---|
| Una muestra medida conservada | 406 B |
| Resumen de una extracción | 1,2 KB |
| Artefacto de simulación comprimido (300 muestras) | 4 KB |
| Base de datos con el entorno recién inicializado | 46 MB |
| Almacén del grafo en el mismo estado | 8,4 MB |
| Imágenes de los servicios a desplegar | 2,7 GB, **medidos en el equipo** el 2026-09-15 |
| Sistema operativo instalado y actualizado | 2,3 GB |
| Archivo de respaldo del intercambio, tal como viene de fábrica | 2,0 GB — **retirado**, véase § 1.4 |
| Cachés de instalación | 0,4 GB — retiradas |
| **Ocupación tras dimensionar, sin datos de operación** | **4,6 GB de 14,3 útiles: el 35 %** |

Una preparación completa —resumen, serie medida a una muestra por segundo,
simulación y hechos semánticos— ocupa del orden de **45 KB**. Mil preparaciones
suponen unos **50 MB** sobre un suelo de unos 55 MB.

**El espacio no es, ni será, la restricción.** La decisión se toma por
fiabilidad del medio y por protección frente a la pérdida.

### 1.3 Horizonte declarado

Este montaje es el **prototipo de un trabajo de grado**, con un horizonte de
operación de meses y bajo supervisión de quien lo construye. No es una
instalación destinada a funcionar años sin atención.

El horizonte no es un detalle de contexto: es lo que decide esta ADR. El
desgaste de una tarjeta por escritura continua es un argumento de peso a escala
de años; a escala de meses, y con el volumen de § 1.1, no lo es. Si el sistema
pasara a operación permanente, esta decisión debe reabrirse, y la alternativa
entonces no es el disco disponible, sino uno de estado sólido.

### 1.2 Medios disponibles

| Medio | Estado comprobado |
|---|---|
| Tarjeta de memoria | 16 GB |
| Disco externo | Toshiba MK5075GSX, 500 GB, 2,5", conexión USB, etiqueta 5 V / 1,0 A |

Estado del disco según su registro interno, leído el 2026-09-15:

| Indicador | Valor | Lectura |
|---|---|---|
| Evaluación general | Superada | No presenta fallo inminente |
| Horas de funcionamiento | 5 030 | Uso moderado para su antigüedad (2012) |
| Ciclos de encendido | 4 932 | Uso intensivo como disco externo |
| **Sectores reasignados** | **216**, en 13 episodios | **Degradación previa real del medio** |
| Sectores pendientes | 0 | Sin deterioro activo |
| Sectores ilegibles | 0 | Sin pérdida de datos conocida |

El disco funciona y no da señales de fallo inmediato, pero ha perdido superficie
de forma progresiva a lo largo de su vida. Un medio sano declara cero sectores
reasignados.

### 1.4 Dimensionado del medio disponible

La instalación de fábrica ocupaba 7 GB de los 14,3 útiles —el 52 %— antes de
guardar un solo dato. Dos terceras partes de ese exceso no eran del sistema ni de
la plataforma:

| Medida aplicada el 2026-09-15 | Libera | Motivo |
|---|---|---|
| Intercambio solo en memoria comprimida, sin archivo de respaldo en la tarjeta | 2,0 GB | El equipo tiene 8 GiB y opera con 0,4: el respaldo no llegaría a usarse. Además el intercambio es la escritura más agresiva que recibiría el medio, de modo que retirarlo gana espacio **y** vida útil |
| Retirada de las cachés de instalación | 0,4 GB | No se vuelven a usar tras instalar |
| Límite de tamaño y rotación de los registros de los contenedores | — | Evita el único crecimiento no acotado que quedaba |

Resultado comprobado tras reinicio completo: **4,6 GB, el 35 %**, con el
intercambio comprimido activo, las imágenes intactas y ningún servicio fallido.

El peor caso previsto es una actualización que conserve la versión anterior para
poder revertirla (ADR-016): dos juegos de imágenes, 2,7 GB adicionales, que dejan
la ocupación en el **54 %**. Los datos de operación, a 45 KB por preparación, no
alteran esta cuenta a ninguna escala prevista.

## 2. Problema de decisión

Se debe definir en qué medio residen el sistema y los datos, cuánto historial se
conserva y cómo se protege la evidencia frente al fallo del medio.

## 3. Alternativas consideradas

### 3.1 Reparto entre medios

| Alternativa | Evaluación |
|---|---|
| Sistema y plataforma completos en una tarjeta de 32 GB, con copia diaria al disco | Ocupa menos de una cuarta parte del medio; elimina del montaje el componente degradado, la dependencia de arranque y el problema de corriente. El fallo sin aviso de la tarjeta se cubre con la copia, que es la protección que corresponde a ese riesgo. **Seleccionada en la revisión.** |
| Sistema en la tarjeta y plataforma de contenedores completa en el disco | Reparto correcto sobre el papel, y seleccionado en la primera redacción. Descartado en la revisión por § 3.3. |
| Todo en el disco externo, arranque incluido | Evita la tarjeta, pero concentra sistema y datos en el componente más antiguo del montaje y exige configurar el arranque por USB. |
| Adquirir una tarjeta de 32 GB | Daría margen sin pensar, pero el margen se consiguió sin gastar: la instalación de fábrica desperdiciaba 2,4 GB en un archivo de intercambio inútil y en cachés. Descartada mientras la ocupación se mantenga bajo el umbral de § 4.8. |

### 3.2 Retención

| Alternativa | Evaluación |
|---|---|
| Podar series antiguas | No libera nada relevante —el historial completo previsto cabe en decenas de megabytes— y destruye evidencia experimental irrepetible. |
| Conservar todo el historial | Coherente con el volumen real y con el valor de cada preparación, que supone un ensayo con su ajuste y su evaluación sensorial. **Seleccionada.** |

### 3.3 Por qué se revisó la decisión el mismo día

La primera redacción situaba la plataforma de contenedores en el disco externo,
razonando que la tarjeta no debía recibir escritura continua. Tres hechos
medidos el mismo día desmontaron ese razonamiento:

1. **El horizonte es de meses, no de años** (§ 1.3). El desgaste por escritura,
   que era el argumento principal, no se materializa en ese plazo con 45 KB por
   preparación.
2. **El disco disponible es el eslabón frágil, no el robusto.** Acumula 216
   sectores reasignados y se desconectó solo durante un borrado intensivo,
   obligando a ampliar el reparto de corriente de los puertos de la plataforma.
   Confiarle la evidencia experimental por evitar el desgaste de una tarjeta
   nueva invierte el riesgo real.
3. **El reparto pretendido no se consigue con la configuración prevista.** La
   versión de la plataforma de contenedores instalada almacena las imágenes a
   través de su componente de ejecución, que ignora la ruta de datos declarada:
   con `data-root` apuntando al disco, los 2,7 GB de imágenes se escribieron
   igualmente en la tarjeta. Sostener el reparto exigiría reubicar también ese
   componente, añadiendo una pieza de configuración que puede revertirse en una
   actualización sin que nadie lo advierta.

La protección contra el fallo sin aviso de una tarjeta no es elegir otro medio:
es **tener copia verificada**, que este ADR ya exigía por otras razones.

## 4. Decisión

1. **El sistema y la plataforma completa residen en la tarjeta de memoria de
   16 GB ya disponible.** No se reparte el sistema entre dos medios y no se
   adquiere otra tarjeta: la instalación se dimensiona para caber en la que hay,
   con las medidas de § 1.4, que la dejan al 35 % y al 54 % en el peor caso
   previsto.
2. **Se conserva todo el historial.** No se establece política de poda.
3. **Se realizará una copia diaria a un medio distinto del principal**, con al
   menos una restauración comprobada antes de iniciar la campaña experimental.
   Una copia que nunca se ha restaurado no acredita nada.
4. **El disco externo pasa a ser destino de copia, no almacén de operación.** Su
   degradación deja de comprometer la evidencia: una copia que falla se detecta
   al verificarla y se repone, mientras que un almacén primario que falla se
   lleva el historial.

   **Comprobado el 2026-09-15.** Con el reparto de corriente ampliado, el disco
   soportó tres ciclos de 20 000 archivos pequeños creados y borrados sin una
   sola desconexión, y sus sectores reasignados siguen en 216, los mismos de
   antes de la prueba. Sirve para el papel que se le asigna. La prueba mueve
   79 MB por ciclo y **no acredita** un volcado masivo ni una restauración
   completa: eso queda por comprobar cuando exista la copia real.
5. **Los servicios no dependen del disco para arrancar.** El arranque de la
   plataforma no espera a ningún medio externo; la ausencia del disco solo
   impide la copia, y esa condición debe ser visible.
6. **El estado del disco se vigilará periódicamente.** La aparición de sectores
   pendientes, o el crecimiento del número de sectores reasignados, obliga a
   retirarlo también como destino de copia.
7. **Mientras el disco permanezca conectado a la plataforma se mantiene ampliado
   el reparto de corriente de sus puertos**, porque con la fuente disponible el
   reparto de fábrica no cubre su consumo en ráfaga y ya provocó una desconexión
   en caliente. La comprobación de que la plataforma no limita por subtensión es
   condición de cada medición.
8. **La ocupación de la tarjeta se registra en cada medición.** Superar el 80 %
   obliga a sustituirla por una mayor antes de continuar: una medición tomada
   sobre un medio casi lleno no describe la capacidad del equipo.

## 5. Justificación

1. **El riesgo no es quedarse sin espacio, es perder la evidencia.** Los números
   de § 1.1 descartan el problema de capacidad en cualquier escenario previsto.
2. **El riesgo se cubre con copia, no con el medio.** Una tarjeta falla sin
   avisar; por eso hay copia diaria verificada. Cambiar de medio para evitar ese
   fallo, y encima a uno que ya está dañado, no reduce el riesgo: lo traslada.
3. **Menos piezas, menos modos de fallo.** Un solo medio elimina la dependencia
   de arranque, la desconexión en caliente y el reparto de corriente al límite.
   Todo eso ocurrió en la puesta en marcha y ninguno habría existido sin el
   disco en el camino de operación.
4. **El disco disponible ya mostró degradación.** Usarlo como destino de copia es
   razonable; confiarle la operación, no.
5. **El horizonte declarado sostiene la elección** (§ 1.3): el desgaste que
   descartaría la tarjeta se mide en años, y este montaje se mide en meses.
6. **Borrar no aporta nada y cuesta evidencia.** Cada preparación es un ensayo
   con su ajuste, su medición y su evaluación sensorial; no se repite.

## 6. Consecuencias

### 6.1 Consecuencias favorables

- La evidencia deja de depender de un medio degradado.
- El arranque no depende de ningún dispositivo externo.
- Desaparecen la desconexión en caliente y el reparto de corriente como riesgos
  del camino de operación.
- El historial completo permanece disponible para el análisis retrospectivo.
- El montaje puede iniciarse con el material ya disponible.

### 6.2 Consecuencias desfavorables

- **La operación se apoya en un medio que falla sin avisar.** Toda la protección
  recae en la copia diaria, que pasa de ser prudente a ser obligatoria: sin
  copia verificada, esta decisión es indefendible.
- El margen depende de las medidas de § 1.4. Si alguien restablece el archivo de
  intercambio, reactiva el mecanismo de fábrica o deja crecer los registros sin
  límite, la tarjeta vuelve al 52 % y la decisión pierde su fundamento. Por eso
  cada medida quedó escrita en el propio equipo, junto a la forma de revertirla.
- El desgaste de la tarjeta no es observable: no hay indicador equivalente al
  del disco, y por eso la decisión queda atada al horizonte de § 1.3.

## 7. Riesgos y tratamiento

| Riesgo | Tratamiento |
|---|---|
| Fallo del disco con evidencia no copiada | Copia diaria a un medio distinto y restauración verificada. |
| Arranque sin el disco montado | El arranque de los servicios espera al montaje; se comprueba tras un corte de energía. |
| Corriente insuficiente para el disco | Reparto de corriente ampliado en la plataforma, ya aplicado, y disco fuera del camino de operación: una desconexión durante una copia se repite, durante una escritura de base de datos corrompe. |
| Degradación progresiva inadvertida | Revisión periódica del registro interno del disco, con criterio de retirada declarado. |
| Desgaste de la tarjeta, ahora con datos y contenedores | Horizonte de meses declarado en § 1.3, copia diaria verificada y registro de la escritura acumulada para saber si la hipótesis se sostiene. |

## 8. Aspectos pendientes

| Pendiente | Condición de cierre |
|---|---|
| Ocupación bajo el umbral | Comprobar en cada medición que la tarjeta se mantiene por debajo del 80 %. Superarlo obliga a revisar esta decisión, no a seguir midiendo. |
| Escritura acumulada en operación | Medirla con la plataforma en marcha, para saber si el desgaste sostiene el horizonte declarado o lo desmiente. Es la medición que respalda esta decisión con un número. |
| Copia diaria y restauración | **Copia automatizada el 2026-10-30** ([`espresso-respaldo.timer`](../../../platform/web/systemd/espresso-respaldo.timer), 03:00, recupera la copia perdida si la Raspberry estaba apagada). Primera copia: 34 s, 138 MB, sumas verificadas; la anterior era del 2026-09-16. **Restauración completa comprobada el 2026-10-30** en el PC, en una plataforma aparte construida en c801b1d: la base restaurada en 3 s, el grafo cargado, migraciones sin nada que aplicar y la API igual que la Raspberry (141 cafés, último sim-00000484, la misma decisión del primer uso). El inventario difería en 26 muestras en vivo llegadas entre el volcado y su conteo; desde entonces `respaldar.sh` cuenta hasta un corte previo al volcado, comprobado con datos entrando. |
| Revisión del estado del disco | Definir la periodicidad y dejar registro de cada lectura, ahora como destino de copia. |
| Arranque sin el disco | Comprobar que la plataforma arranca y opera con el disco ausente, y que la imposibilidad de copiar es visible. |

## 9. Verificación de la decisión

Se verificará, como mínimo:

1. corte de energía y arranque posterior con los servicios operativos, sin
   depender de ningún medio externo;
2. copia diaria generada y **restaurada** en un entorno separado;
3. ocupación de la tarjeta por debajo del 80 % en cada medición;
4. escritura acumulada medida y contrastada con el horizonte de § 1.3;
5. lectura del estado del disco sin sectores pendientes, como destino de copia.

## 10. Trazabilidad

- [ADR-002](ADR-002-persistencia-interna-y-almacenamiento-externo.md): el
  historial reside fuera del dispositivo.
- [ADR-010](ADR-010-residencia-y-memoria-del-almacen-rdf.md) y
  [ADR-012](ADR-012-canal-de-comunicacion-con-la-plataforma-externa.md):
  servicios y datos que la plataforma conserva.
- Alcance de producción de la plataforma externa.
- Requisitos: RF-014, RF-015, RF-016 y RNF-006.

## 11. Criterios de reconsideración

La decisión deberá revisarse si el volumen de datos crece en un orden de
magnitud no previsto, si el disco presenta deterioro activo, o si la instalación
pasa a requerir disponibilidad continua, en cuyo caso la copia diaria resultaría
insuficiente frente a una redundancia en línea.
