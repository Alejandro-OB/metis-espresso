# ADR-008 — Protección eléctrica mínima responsable del prototipo supervisado

**Estado:** Aceptada (2026-09-01)

## 1. Contexto

El prototipo modifica una cafetera Clase II de 120 VAC. Sus cargas son el
calentador de 1050 W, la bomba JYPC-503A de 55 W y la fuente aislada
`HLK-10M05`, con una corriente nominal de 1050/120 + 55/120 + 0,25 = 9,458 A.
La caldera conserva dos fusibles térmicos `MICROTEMP G4A00`, `Tf 192 °C`, y la
bomba su protector térmico original. El prototipo funciona siempre supervisado.

En Colombia no se consiguieron fusibles de retardo ni portafusibles de más de
10 A o para circuito impreso. Se usan los fusibles rápidos `F12AL250V` y
`F2AL250V` en portafusibles de panel 5 × 20 de 10 A / 250 VAC. El procedimiento
de desconexión para intervenir está en ADR-005.

## 2. Alternativas descartadas

| Alternativa | Razón |
|---|---|
| Protección ampliada con `T15A`, fusibles de bomba e IRM, TMOV y filtro EMI comercial | Volumen y componentes innecesarios para un prototipo supervisado. |
| Fusible general único `F12AL250V` | El portafusible de 10 A trabajaría al 95 %; ante un fallo cedería el soporte y no el fusible. |
| Fusible general de 10 A | La operación normal queda al 95 % del nominal, con aperturas molestas en cada calentamiento. |
| Filtro EMI comercial de 15–20 A | Voluminoso y costoso. |
| Interruptor termomagnético en miniatura | No cabe dentro de la cafetera. |

## 3. Decisión

### 3.1 Topología aprobada

```text
enchufe de la cafetera (120 VAC)
  │
entrada común: fusible térmico 110 °C + varistor 14D241K entre los dos conductores
               CX_EMI 100 nF clase X2 275/305 VAC ∥ R_DISCH 1 MΩ
  │
  ├── PORTAFUSIBLE A + F12AL250V cerámico rápido
  │     └── SSR → G4A00 → calentador → G4A00          8,75 A → 87 % del soporte
  │
  └── PORTAFUSIBLE B + F2AL250V cerámico rápido
        ├── módulo AC con TRIAC y cruce por cero → protector térmico original → bomba
        └── HLK-10M05 → 5 V aislados                  0,71 A →  7 % del soporte
```

La supresión L–N va antes de los portafusibles, donde absorbe los transitorios
de red; el fusible térmico desconecta el varistor si se calienta. `CX_EMI` debe
estar certificado como clase X2. El tramo previo al reparto lo protege el
interruptor termomagnético de la instalación.

### 3.2 Fusibles y protecciones térmicas

Dos ramas bajan el peor caso del portafusible del 95 % al 87 %, y un fallo en
la bomba o la fuente no deja sin alimentación al calentador. El cartucho de
12 A es cerámico, de mayor capacidad de ruptura que uno de vidrio.

Se acepta que entre 10 A y 12 A el portafusible A quede sobrecargado sin que el
fusible abra, porque solo ocurre en condición de fallo. Una apertura molesta no
autoriza subir a 15 A.

La bomba comparte el fusible de la fuente. Todo conductor de su rama anterior
al protector térmico debe coordinarse con `F2AL250V`; si no, se añade un
fusible antes de energizar, nunca una compensación por firmware.

La supresión usa el capacitor Songtian `X2P2104KN1B0130120060ES0` de
100 nF/275 VAC y la resistencia ignífuga Yageo `RSF100JT-73-1M` de 1 MΩ/1 W. El
fusible térmico previsto era de 98 °C; se abrió al soldarlo y se montó uno de
110 °C, que también abre antes de que un varistor en fuga térmica arda.

Los fusibles quedan en el panel y cada rama entra a `PCB-POTENCIA` por su borne,
`TB1` o `TB2`. Esa placa no se separa del resto sin desoldar.

### 3.3 Elementos que no se incorporan

Fusible propio de la bomba, módulo EMI comercial, bobina de modo común,
capacitores Y o conexión al chasis (la máquina sigue siendo Clase II),
contactor, riel DIN y pulsador de emergencia. El firmware (ADR-005, ADR-007)
complementa esta protección, no la reemplaza.

### 3.4 Módulo de control de la bomba

Es el *Módulo Control AC de Fase y On-Off* de Electrónica Plug and Play
([ficha](https://www.electronicaplugandplay.com/modulos-plug-and-play/product/286-modulo-control-fase-ac)).
Su barrera la cruzan los optoacopladores `MOC3021` (7500 V) y `PC817` (5000 V),
con 33 mm de separación. Admite 6 A con disipador y trae snubber RC. El pulso
`SYNC` dura 100 µs y la lógica se alimenta desde `+3V3`. El TRIAC no tiene
`dV/dt` declarado; su aptitud con el solenoide se establece con el punto 9 de
§ 5.

## 4. Consecuencias

El esquema solo vale para construcción y pruebas supervisadas; no autoriza uso
desatendido, comercialización ni declaración de conformidad. Si aparece
interferencia conducida, se caracteriza antes de añadir filtrado.

## 5. Verificación obligatoria antes de energizar

1. Las referencias recibidas coinciden con las seleccionadas y cada cartucho con
   su portafusible.
2. Los dos `G4A00` y el protector de la bomba tienen continuidad y contacto
   térmico, sin puentearlos.
3. Cableado, conectores y pistas se coordinan con el fusible de su rama.
4. Se revisan distancias, cubierta, la serie fusible térmico–varistor y el
   paralelo capacitor–resistencia.
5. Con instrumentación adecuada, se miden corriente RMS y temperaturas.
6. En la primera puesta en marcha supervisada ningún fusible abre ni se
   calienta de forma anormal. Si el portafusible A se calienta de forma anormal
   en un calentamiento completo, se detiene la prueba hasta conseguir un soporte
   de mayor calibre.
7. Retirar el enchufe elimina la alimentación de las tres ramas y las salidas
   siguen inactivas al volver a energizar.
8. La prueba se detiene ante cualquier comportamiento anormal.
9. El módulo de la bomba se prueba en banco con un microcontrolador de pruebas,
   no con el firmware del sistema, cuyo guardián inhibe las salidas. Se barre el
   ángulo de disparo con la bomba bombeando agua: la tensión debe variar de
   forma continua y, con el retardo al máximo, la bomba debe detenerse. Si sigue
   conduciendo, el módulo no sirve. Se anota el rango útil de retardo.

## 6. Trazabilidad

Decisiones relacionadas: ADR-005, ADR-006 y ADR-007.

## 7. Referencias técnicas

- [Littelfuse Fuseology](https://www.littelfuse.com/assetdocs/fuseology-selection-guide?assetguid=fa4aa360-f6c4-4eec-88a6-3d7ec3fe57d5).
- [Varistor `14D241K`](https://www.digikey.com/en/products/detail/bourns-inc/MOV-14D241K/2799114).
- [Hi-Link `HLK-10M05`](https://www.hlktech.net/index.php?id=134).
- [TDK — filtros EMC, capacitores X y bobinas](https://www.tdk-electronics.tdk.com/download/528632/7f30b0110a521bb506ace36f0c56af5c/pdf-technical-information.pdf).
- [Songtian `X2P2104KN1B0130120060ES0`](https://www.digikey.com/en/products/detail/songtian-electronic-technology-hk-limited/X2P2104KN1B0130120060ES0/13985066).
- [Yageo `RSF100JT-73-1M`](https://www.digikey.com/en/products/detail/yageo/RSF100JT-73-1M/9162046).
