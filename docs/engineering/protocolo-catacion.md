# Protocolo de catación con el barista

Este es el protocolo que se siguió en la sesión de catación del 4 de noviembre
de 2026, con la canastilla presurizada de dos tazas. Participaron dos personas:
el **operador**, que maneja la máquina y la aplicación web (Bitácora de
espresso), y el **barista**, que prepara, prueba y decide.

## Antes de empezar

1. Llenar el depósito de agua. Encender la máquina, hacer dos pasadas de agua de
   20 s sin café y esperar a «lista».
2. Poner al aire, lejos de la máquina, la sonda DS18B20 conectada al computador
   por USB. Con ella se toma la temperatura del cuarto.
3. Poner el refractómetro en cero y tener a mano una báscula externa de 0,1 g.
4. Acordar con el barista dos esperas, iguales en todos los cafés: una tras
   «lista», antes de hacer el café, y otra antes de probarlo.
5. Explicarle que la temperatura indicada es la del **agua** (92 °C de agua
   equivalen a 103,8 °C de caldera) y que va de **85,7 a 93,0 °C**.
6. El barista entra a la aplicación con su cuenta. Se registra el café que se
   va a usar.
7. En las pasadas de agua, comprobar que las curvas llegan a la aplicación y que
   no aparece el aviso de desfase de hora. Si aparece, no se empieza.

## En todos los cafés

1. Anotar en «Preparar café» la temperatura del cuarto que marca la sonda, la
   dosis que pesó el barista y la molienda.
2. Esperar lo acordado tras «lista», pulsar «Iniciar preparación» y hacer el
   café.
3. Cuando termine de gotear, pesar la taza y tomar la muestra para el TDS.
4. Si el café se prueba, esperar lo acordado. El barista lo prueba y se anota
   la ficha, con los defectos (o «Ninguno») y si lo acepta o no.
5. Tomar tres lecturas de TDS de la muestra.
6. Anotar la masa de la taza y el TDS en el detalle del café.

**Cuando el barista quiere cambiar algo**, dice qué cambia (molino, temperatura,
tiempo o dosis), qué quiere corregir y qué espera. En la aplicación, bajo la
cata, se elige «Sí, hacer un ajuste», se escribe el valor nuevo, el defecto en
«Qué buscas corregir» y lo que espera en «Nota», y se pulsa «Registrar ajuste».
El café siguiente se marca «De comprobación».

**Si pasan más de 10 min sin café**, se pasa agua 20 s sin café y se espera a
«lista».

## 1. El barista afina la receta desde el perfil vigente

1. Se usa el perfil vigente de la máquina (13 g a 38 clics).
2. Se prepara un primer café con ese perfil. El barista lo prueba y dice qué le
   parece.
3. Si quiere cambiar algo, se registra el ajuste y el café siguiente lo
   comprueba.
4. Se repite hasta que quede conforme. Su receta final es el perfil en uso con
   la molienda y la dosis de ese café, y se anotan.

## 2. El barista repite su receta final tres veces

1. Se preparan y prueban tres cafés con la receta final. En los tres se
   cronometra con el celular el tiempo desde que se pulsa el botón hasta que se
   apaga la bomba, y se anota.
2. En «Perfiles», sobre la receta, se abre «Ajustes avanzados», se marcan los
   tres cafés, se pulsa «Calcular tolerancia» y luego «Usar en la máquina». Se
   anota la tolerancia de la dosis.

## 3. Pruebas pedidas al barista

1. **Café largo.** El barista dice cuántos segundos más harían que el café se
   pase. Se sube el «Tiempo máximo» en la aplicación, se hace el café y se
   prueba. Después se vuelve a la receta en «Perfiles guardados».
2. **Agua más fría.** El barista elige una temperatura al menos 2 °C por debajo
   de la de la receta. Bajar la temperatura tarda varios minutos, así que se
   espera a «lista». Se hace un café con esa temperatura y otro con la receta,
   tomada de «Perfiles guardados», sin decirle cuál es cuál. Se prueban los dos
   y se anota cuál fue cada uno.
3. **Si hay tiempo, sin probar.** Se prepara un café con menos dosis (receta
   − 2 × la tolerancia de la dosis), uno con más dosis (receta + 2 × la
   tolerancia) y uno que el barista corta a la mitad.

## 4. Si queda tiempo, el barista vuelve a mejorar

Se procede igual que en el paso 1: el barista cambia una cosa por café, dice
qué espera y prueba el resultado.

## Al terminar

Se respalda la base de datos. En la Raspberry Pi, desde la carpeta
`platform/web` del repositorio:

```bash
./respaldar.sh
```
