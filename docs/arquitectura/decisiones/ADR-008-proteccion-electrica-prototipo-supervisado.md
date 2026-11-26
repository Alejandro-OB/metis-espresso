# ADR-008 — Protección eléctrica mínima responsable del prototipo supervisado

**Estado:** Aceptada, revisada
**Fecha de decisión:** 2026-09-01  
**Última revisión:** 2026-09-21
**Ámbito:** entrada de red, protección de ramas, sobretemperatura, transitorios y supresión EMI básica

## 1. Contexto

El prototipo modifica una cafetera Clase II alimentada a 120 VAC y sustituye sus
tarjetas originales por un montaje modular. Las cargas conocidas son el
calentador de 1050 W, la bomba JYPC-503A de 55 W y la fuente aislada `HLK-10M05`.
La corriente nominal aritmética conocida es:

```text
1050/120 + 55/120 + 0,25 = 9,458 A
```

La unidad conserva dos fusibles térmicos `MICROTEMP G4A00`, `Tf 192 °C`,
sujetos al boiler, y un protector térmico original sujeto a la bobina de la
bomba. La arquitectura anterior añadía un fusible principal `T15A`, un fusible
`T1,25A` para la bomba, un fusible `T1,6A` para la IRM y un TMOV. Ese esquema se
revisó porque el primer montaje será un prototipo compacto, supervisado y no
destinado a operación desatendida ni certificación como producto. Como no fue
posible conseguir los fusibles retardados seleccionados, el prototipo utilizará
los fusibles disponibles `F12AL250V` y `F2AL250V`, ambos de actuación rápida.

La simplificación no autoriza eliminar las barreras básicas frente a 120 VAC.
Este ADR define la protección general contra sobrecorriente, la protección
selectiva de la rama lógica, la conservación de las protecciones térmicas
originales y la barrera entre red y baja tensión. El procedimiento de
desconexión para intervenir se establece en ADR-005.

## 2. Alternativas consideradas

| Alternativa | Evaluación |
|---|---|
| Protección ampliada con `T15A`, fusibles de bomba e IRM, TMOV y filtro EMI comercial | Proporciona más segmentación, pero añade volumen y componentes que no son necesarios para iniciar un prototipo siempre supervisado. No seleccionada. |
| Protección mínima formada solo por el fusible general y los térmicos originales | Reduce al máximo la lista, pero deja la rama pequeña de la IRM protegida únicamente por un fusible dimensionado para toda la cafetera. No seleccionada. |
| Protección mínima responsable con fusible general, fusible de IRM, protecciones térmicas originales, TMOV y supresión EMI L–N básica | Seleccionada. Mantiene las barreras que responden a los riesgos principales sin convertir el montaje en una distribución industrial. |
| Filtro EMI comercial de 15–20 A en toda la entrada | Funcional, pero voluminoso y costoso para la etapa actual. No seleccionado. |
| Filtro discreto con bobina de modo común | Se deja como evolución condicionada a evidencia de interferencia. La primera versión no monta la bobina. |
| Fusibles disponibles `F12AL250V` y `F2AL250V` | Seleccionados por disponibilidad local para el prototipo supervisado. Son de actuación rápida y se comprobarán mediante inspección básica y puesta en marcha supervisada. |

## 3. Decisión

### 3.1 Topología aprobada

**Revisada el 2026-09-17.** La topología original era un fusible general único,
`F_MAIN`, del que colgaban las tres ramas. No sobrevivió al portafusible que se
consiguió; la vigente es ésta, y el porqué del cambio está en la revisión de la
§ 3.2, que se conserva entera porque el motivo importa tanto como el resultado.

```text
120 VAC
  │
enchufe de alimentación de la cafetera
  │
entrada común: fusible térmico 98 °C + varistor 14D241K entre los dos conductores
               CX_EMI 100 nF clase X2 275/305 VAC ∥ R_DISCH 1 MΩ
  │
  ├── PORTAFUSIBLE A + F12AL250V cerámico rápido, 250 VAC
  │     └── SSR → G4A00 → calentador → G4A00
  │
  └── PORTAFUSIBLE B + F2AL250V cerámico rápido, 250 VAC
        ├── módulo AC con TRIAC y cruce por cero → protector térmico original → bomba
        └── HLK-10M05 → 5 V aislados
```

El fusible térmico de 98 °C, el `14D241K`, `CX_EMI` y `R_DISCH` se ubican en la
entrada común, **aguas arriba de los dos portafusibles**, y dentro de la cubierta
de potencia. El tramo entre el enchufe y ese reparto queda cubierto por el
interruptor termomagnético de la instalación y no por un fusible propio.

El `14D241K` limita transitorios L–N; el fusible térmico se instala en serie y junto al varistor
para interrumpirlo si se calienta de forma anormal. El capacitor X2 atenúa
ruido diferencial y la resistencia descarga el capacitor al retirar la
alimentación. Ninguno de estos elementos sustituye a los fusibles de rama.

`CX_EMI` debe estar marcado y certificado como clase X2 para conexión directa
entre conductores de red. `R_DISCH` será retardante a la llama, de 0,5 W y con
tensión de trabajo compatible con 120 VAC. No se permiten capacitores comunes
de 50 V o 100 V en esa posición.

### 3.2 Fusibles y protecciones térmicas

| Posición | Decisión |
|---|---|
| Entrada completa | **Revisado el 2026-09-17**: no hay fusible de entrada completa. `F12AL250V` pasa a proteger la rama del calentador. El cartucho recibido es **cerámico**, de capacidad de ruptura muy superior a uno de vidrio, lo que cierra a favor la verificación pendiente. |
| Calentador | **Revisado el 2026-09-17**: sí lleva fusible de rama, `F12AL250V` en su propio portafusible, porque ya no existe fusible general. Se conservan los dos `MICROTEMP G4A00`, `Tf 192 °C`. |
| Bomba | **Revisado el 2026-09-17**: comparte el `F2AL250V` del portafusible B con el `HLK-10M05`, porque ya no existe fusible general del que colgar. No lleva fusible propio y se conserva el protector térmico original. |
| HLK-10M05 | Fusible `F2AL250V`, rápido, 2 A y 250 VAC; portafusible compatible con sus dimensiones. La corriente de irrupción de la fuente se observará durante la puesta en marcha. |

Se dispone además de un fusible térmico de 110 °C y 250 V/10 A como repuesto.
No se instala junto con el de 98 °C ni reemplaza los dos térmicos originales
del boiler.

> **Revisión del 2026-10-02 — el termofusible montado es el de 110 °C.** El de
> 98 °C quedó abierto al soldarlo, por el calor del cautín, que es el fallo típico
> de esta pieza: medido sobre las patas, junto al cuerpo, no conducía. No había
> otro de 98 °C y se montó el repuesto de 110 °C, cortando las patas del dañado y
> soldando el nuevo sobre ellas para no levantar los pads de red.
>
> Cumple la misma función. Lo que se le exige es abrir antes de que un varistor
> en fuga térmica llegue a arder, y 110 °C sigue por debajo de esa temperatura;
> los varistores con protección térmica integrada abren en un rango parecido, dato
> de conocimiento general que no se verificó contra una hoja de datos concreta.
> Dentro de la cafetera tiene además una ventaja: el aire junto al boiler se
> calienta y uno de 110 °C es menos propenso a abrir solo por el ambiente.
>
> Verificado con multímetro tras el cambio: continuidad entre sus patas y entre
> la pata del varistor que va a él y el neutro, y sin continuidad entre la línea
> de entrada y el neutro. Ya no queda repuesto: si este abre, hay que comprar otro.
> Donde este ADR dice 98 °C, en la entrada común vigente, debe leerse 110 °C.

La supresión L–N se implementa con varistor `14D241K`, fusible térmico de 98 °C,
capacitor X2 Songtian
`X2P2104KN1B0130120060ES0` de 100 nF/275 VAC y resistencia Yageo
`RSF100JT-73-1M` de 1 MΩ/1 W, película de óxido metálico e ignífuga. Estas
referencias sustituyen descripciones comerciales genéricas en la orden de
compra, pero no eliminan la validación del montaje recibido.

La carga conocida corresponde al 78,8 % de `F12AL250V`. Esto supera ligeramente la
recomendación general de operación al 75 % usada como punto de partida por
Littelfuse. La selección de 12 A es una decisión consciente para el prototipo
supervisado, no una certificación: idealmente se verificará con la corriente RMS real,
la temperatura interna y ciclos repetidos. Una apertura molesta no autoriza
subir directamente a 15 A; primero se revisan corriente, ambiente, cableado y
portafusible.

La ausencia de fusible de bomba implica que cualquier conductor y conector de
esa rama que quede antes del protector térmico debe ser compatible con la
protección general o quedar revisado expresamente. Si el arnés o el módulo no
pueden coordinarse con `F12AL250V`, se restituirá un fusible de rama antes de
energizar; no se compensará esa incompatibilidad mediante firmware.

#### Revisión del 2026-09-17 — la protección se reparte en dos ramas

El portafusible conseguido es **de panel, cartucho 5 × 20, 10 A / 250 VAC**, y no
se consigue de más amperaje ni en versión para circuito impreso. Eso rompe una
correspondencia que § 5.1 exige comprobar: **el fusible general de 12 A no
corresponde a su portafusible**. Con la carga conocida de 9,458 A, el soporte
trabajaría al **95 % de su capacidad**, y el componente que cedería ante un fallo
sería el portafusible y no el fusible, que es el único diseñado para hacerlo de
forma controlada.

La protección general única deja de ser posible. Se sustituye por **dos ramas,
una por portafusible**:

```text
enchufe de alimentación de la cafetera
  │
  ├── entrada común: fusible térmico 110 °C + varistor 14D241K
  │                  CX_EMI 100 nF X2 ∥ R_DISCH 1 MΩ
  │
  ├── PORTAFUSIBLE A + F12AL250V cerámico rápido
  │     └── SSR → G4A00 → calentador → G4A00          8,75 A → 87 % del soporte
  │
  └── PORTAFUSIBLE B + F2AL250V cerámico rápido
        ├── módulo AC con TRIAC y cruce por cero → protector térmico → bomba
        └── HLK-10M05 → 5 V aislados                  0,71 A →  7 % del soporte
```

Esto **revisa dos decisiones de § 3.2**: ya no hay «fusible de entrada completa»,
y el calentador **sí** pasa a tener fusible de rama. El reparto baja el peor caso
del 95 % al 87 % y añade selectividad: un fallo en la bomba o en la fuente deja de
poder dejar sin alimentación al calentador.

**La supresión L–N permanece en la entrada común**, aguas arriba de los dos
portafusibles. Es donde tiene que estar para absorber transitorios de red, y
conserva su propia protección: la desconexión térmica en serie con el varistor.
El tramo entre el enchufe y el reparto queda cubierto por el interruptor
termomagnético de la instalación, no por un fusible propio.

**El compromiso que se acepta.** Con un fusible de 12 A en un portafusible de
10 A queda una ventana entre ambos valores en la que el soporte está sobrecargado
y el fusible aún no ha abierto. Se acepta porque solo se alcanza **en condición de
fallo**, nunca en operación normal, y porque la alternativa —bajar el fusible a
10 A— lleva la operación **normal** al 95 % de su nominal, con aperturas molestas
en cada calentamiento. Para un prototipo que solo funciona bajo supervisión, un
riesgo acotado al fallo es preferible a un defecto en el uso diario.

Se descartaron por indisponibilidad, no por criterio: fusibles de retardo,
portafusibles de más de 10 A y portafusibles para circuito impreso. Un
interruptor termomagnético en miniatura se descartó por no caber dentro de la
cafetera.

**Ambos fusibles quedan en el panel**, con dos consecuencias para
`PCB-POTENCIA`: `F2` deja de tener huella en la placa y las dos ramas fusionadas
**entran** por los bornes de distribución `TB1` y `TB2`, que pasan a repartir una
rama cada uno en lugar de los cuatro puntos comunes de `TB_SW_A`. Ver
aislamiento y ancho de cobre.

**Verificación que esta revisión añade a § 5**: medir la **temperatura de ambos
portafusibles** durante un calentamiento completo. Deja de ser una comprobación
rutinaria y pasa a ser la que valida esta decisión; un calentamiento anormal del
portafusible A obliga a detener el ensayo y a conseguir un soporte de mayor
calibre antes de continuar.

#### Revisión del 2026-09-16 — las placas ya no se separan sin desoldar

No se consiguieron los cuatro conectores de los dos cables entre placas, sino dos
cables con conector en un solo extremo: `PCB-CONTROL` conserva sus conectores, y
en `PCB-MANDO` y `PCB-POTENCIA` el otro extremo se suelda directamente.

La decisión de arquitectura modular pertenece a ADR-006 y allí debe recogerse el
cambio de interfaz. Aquí importa por una consecuencia de seguridad: **la placa
conectada a la red deja de poder separarse del resto sin desoldar**. Cualquier
procedimiento de intervención, inspección o sustitución que supusiera desconectar
`PCB-POTENCIA` tirando de un conector deja de ser aplicable, y la verificación
de § 5 debe ejecutarse contando con ello.

### 3.3 Elementos que no se incorporan

- ~~fusible independiente del calentador~~ — **lo lleva desde el 2026-09-17**:
  `F12AL250V` en el portafusible A. Esta viñeta describía la topología del fusible
  general y la revisión la dejó sin objeto;
- fusible propio de la bomba, que sigue sin llevarlo: comparte el del
  portafusible B con la fuente;
- módulo EMI comercial de entrada;
- bobina de modo común en la primera población;
- capacitores Y o conexión improvisada al chasis, porque la máquina conserva su condición Clase II sin PE;
- contactor, riel DIN, relé intermedio o pulsador industrial dedicado de emergencia.

### 3.4 Complemento de firmware

El comportamiento del supervisor ante arranque, reinicio, actualización,
alarmas y fallos se define en ADR-005 y ADR-007. En el ámbito eléctrico,
esas medidas complementan, pero no reemplazan, los dos fusibles de rama, los térmicos
originales, el aislamiento y las protecciones de este ADR.

## 4. Consecuencias

- Se utilizan los fusibles disponibles `F12AL250V` y `F2AL250V`, además de un
  fusible térmico asociado al varistor, de 110 °C desde la revisión del
  2026-10-02.
- La rama IRM conserva protección selectiva y el conjunto mantiene protección general.
- El `14D241K` y el capacitor X2 añaden protección contra transitorios y supresión EMI básica; el fusible térmico aporta la desconexión del varistor ante calentamiento anormal.
- El prototipo no incorpora todavía un filtro EMI completo; si aparecen reinicios, errores de ADC o interferencia conducida, se caracterizará el fenómeno antes de añadir una bobina o red adicional.
- El esquema es válido únicamente para construcción y pruebas supervisadas. No autoriza uso desatendido, comercialización ni declaración de conformidad.

> **Revisión del 2026-09-17 — el módulo dimmer queda identificado.** La unidad
> recibida es el *Módulo Control AC de Fase y On-Off* de Electrónica Plug and Play
> ([ficha](https://www.electronicaplugandplay.com/modulos-plug-and-play/product/286-modulo-control-fase-ac)).
> Lo que decidía su admisibilidad está comprobado sobre la pieza: **los dos caminos
> que cruzan su barrera van por optoacoplador** —`MOC3021` de 7500 V para el disparo
> y `PC817` de Sharp de 5000 V para el cruce por cero—, con 33 mm de separación
> medidos. El fabricante declara 3 A sin disipador y **6 A con el disipador que la
> unidad trae montado**, frente a los 0,71 A de la bomba, y un **snubber RC previsto
> para cargas inductivas**, que se confirma en la placa por su serigrafía.
>
> Lo que **no** queda establecido: el TRIAC no lleva marca legible y el fabricante no
> publica su referencia, de modo que no hay `dV/dt` ni `dI/dt` declarados. La bomba
> es un solenoide —factor de potencia bajo y picos por semiciclo— y por tanto una
> carga más exigente que la inductiva típica. Su aptitud solo puede establecerse por
> **ensayo funcional**, que se añade como punto 9 de § 5. No es un requisito de
> seguridad —el aislamiento ya está verificado y el fallo posible es que la bomba no
> obedezca al control, acotado por `F2AL250V`— sino de diseño: si el módulo no sirve,
> su sustituto puede traer otro pinout y obligaría a rehacer `J8` en la placa.
>
> De paso quedan fijados dos datos que necesita el firmware: el pulso `SYNC` dura
> **100 µs** y la lógica admite **3 a 5,5 V**, lo que confirma alimentarla desde
> `+3V3`.

## 5. Verificación obligatoria antes de energizar

1. Verificar que las referencias recibidas coincidan con las seleccionadas, sus marcados sean legibles y cada cartucho corresponda a su portafusible; no aceptar sustituciones sin revisión.
2. Confirmar continuidad, mangas, sujeción y contacto térmico de los dos `G4A00` y del protector original de la bomba, sin puentearlos ni ensayarlos destructivamente.
3. Comprobar que cableado, conectores, pistas y arneses se coordinan con el fusible de **su propia rama** —`F12AL250V` en la del calentador, `F2AL250V` en la de bomba y fuente— y con la temperatura interna. La coordinación dejó de ser única el 2026-09-17.
4. Revisar orientación, distancias, cubierta, conexión en serie del fusible térmico con el `14D241K` y conexión paralela del conjunto X2/resistencia de descarga.
5. Si se dispone de instrumentación adecuada, medir corriente RMS y temperatura de fusibles, portafusibles, cableado, SSR, dimmer y `HLK-10M05` durante calentamiento y extracción.
6. Como mínimo, realizar una primera puesta en marcha supervisada y comprobar que `F2AL250V` y `F12AL250V` no abren, no se calientan de forma anormal y no producen olor, ruido o comportamiento inesperado.
7. Verificar que retirar manualmente el enchufe elimina la alimentación de las
   tres ramas y que las salidas permanecen inactivas al volver a energizar.
8. Mantener la prueba supervisada y detenerla ante cualquier comportamiento anormal. Esta decisión no autoriza operación desatendida ni constituye certificación eléctrica.
9. Ensayar el módulo dimmer en banco, fuera de la cafetera y con un microcontrolador
   de pruebas —nunca con el firmware del sistema, cuyo guardián inhibe las salidas—,
   alimentando la bomba con agua a través de `F2AL250V` y barriendo el ángulo de
   disparo. Se comprueba que la tensión sobre la carga varía de forma continua y que
   al llevar el retardo al máximo la bomba se detiene de verdad. Si se queda
   conduciendo, el TRIAC falla en conmutación con carga inductiva y el módulo no
   sirve. Anotar el rango útil de retardo, que es el que usará el control.

## 6. Trazabilidad

- Requisitos: RF-006, RF-011–RF-012 y RF-014; RNF-001–RNF-003 y RD-001.
- Decisiones relacionadas: ADR-005, ADR-006 y ADR-007.
- Documentos afectados: arquitectura física, lista de adquisición y esquema funcional de tres placas.

## 7. Referencias técnicas

- [Littelfuse Fuseology — selección, reducción y validación de fusibles](https://www.littelfuse.com/assetdocs/fuseology-selection-guide?assetguid=fa4aa360-f6c4-4eec-88a6-3d7ec3fe57d5).
- [Varistor `14D241K` — características de referencia](https://www.digikey.com/en/products/detail/bourns-inc/MOV-14D241K/2799114).
- [Hi-Link `HLK-10M05` — ficha del fabricante](https://www.hlktech.net/index.php?id=134).
- [TDK — información técnica sobre filtros EMC, capacitores X y bobinas](https://www.tdk-electronics.tdk.com/download/528632/7f30b0110a521bb506ace36f0c56af5c/pdf-technical-information.pdf).
- [Songtian `X2P2104KN1B0130120060ES0` — capacitor X2 seleccionado](https://www.digikey.com/en/products/detail/songtian-electronic-technology-hk-limited/X2P2104KN1B0130120060ES0/13985066).
- [Yageo `RSF100JT-73-1M` — resistencia de descarga seleccionada](https://www.digikey.com/en/products/detail/yageo/RSF100JT-73-1M/9162046).
