# Protocolo de catación con el barista

4-nov-2026 · canastilla presurizada de dos tazas · **operador** (Bitácora y
máquina) y **barista** (prepara, prueba y decide).

Resultados esperados y análisis: `protocolo-catacion-casos.md`.

## Antes de empezar

1. Fuera de casa: portátil al wifi «PISO 4» de la Pi; internet por USB del celular.
2. Llenar el depósito de agua. Encender la máquina, hacer dos pasadas de agua de 20 s sin café y esperar a
   «lista».
3. Poner al aire, lejos de la máquina, la sonda DS18B20 de los ensayos,
   conectada al PC por USB. Con ella se toma la temperatura del cuarto.
4. Refractómetro en cero. Báscula externa de 0,1 g.
5. Acordar con el barista dos esperas, iguales en todos los cafés: tras «lista»
   antes de hacer el café, y antes de probarlo.
6. Aclararle que la temperatura es la del **agua** (92 °C de agua = 103,8 °C de
   caldera) y que va de **85,7 a 93,0 °C**.
7. El barista entra a la Bitácora con su cuenta. Registrar el café que se va a
   usar.
8. En las pasadas de agua, comprobar que llegan las curvas a la Bitácora y que
   no aparece el aviso de desfase de hora. Si aparece, no empezar.

## En todos los cafés

1. Anotar en «Preparar café» la temperatura del cuarto que marca la sonda, la
   dosis que pesó el barista y la molienda.
2. Esperar lo acordado tras «lista», pulsar «Iniciar preparación» y hacer el
   café.
3. Cuando termine de gotear, pesar la taza y tomar la muestra para el TDS.
4. Si se prueba: esperar lo acordado, el barista prueba y se anota la ficha,
   con los defectos (o «Ninguno») y si lo acepta o no.
5. Leer el TDS: tres lecturas de la muestra.
6. Anotar la taza y el TDS en el detalle del café.

**Cuando el barista quiere cambiar algo:** dice qué cambia (molino,
temperatura, tiempo o dosis), qué quiere corregir y qué espera. Bajo la cata:
«Sí, hacer un ajuste» → valor nuevo → el defecto en «Qué buscas corregir» → lo
que espera en «Nota» → «Registrar ajuste». El café siguiente se marca «De
comprobación».

**Si pasan más de 10 min sin café:** pasar agua 20 s sin café y esperar a
«lista».

## 1. El barista afina la receta desde el perfil vigente

1. Usar el perfil vigente de la máquina (13 g a 38 clics).
2. Primer café con ese perfil. El barista lo prueba y dice qué le parece.
3. Si quiere cambiar algo, se registra el ajuste y el café siguiente lo
   comprueba.
4. Se repite hasta que quede conforme. Su receta final es el perfil en uso con
   la molienda y la dosis de ese café. Anotarlas.

## 2. Repite su receta final tres veces

1. Tres cafés con la receta final, probados. En los tres, cronometrar con el
   celular desde que se pulsa el botón hasta que se apaga la bomba, y
   anotarlo.
2. Perfiles → la receta → «Ajustes avanzados» → marcar los tres cafés →
   «Calcular tolerancia» → «Usar en la máquina». Anotar la tolerancia de la
   dosis.

## 3. Las pruebas que se le piden

1. **Café largo:** el barista dice cuántos segundos más harían que se pase.
   Subir el «Tiempo máximo» en la Bitácora y hacer el café. Se prueba. Volver
   a la receta en «Perfiles guardados».
2. **Agua más fría:** el barista elige una temperatura al menos 2 °C por debajo
   de la receta. Bajar la temperatura tarda varios minutos: esperar a «lista».
   Hacer un café con ella y otro con la receta, en «Perfiles
   guardados», sin decirle cuál es cuál. Se prueban los dos. Anotar cuál fue
   cada uno.
3. **Si hay tiempo, sin probar:** un café con menos dosis (receta − 2 × la
   tolerancia de la dosis), uno con más (receta + 2 × la tolerancia) y uno que
   el barista corta a la mitad.

## 4. Si queda tiempo, vuelve a mejorar

Igual que en el paso 1: cambia una cosa por café, dice qué espera y prueba el
resultado.

## Al terminar

Respaldar la base de datos desde el portátil:

```
ssh espresso-pi 'cd ~/espresso/apps/web && ./respaldar.sh'
```
