# Sistema de diseño web v1

La dirección es **verde profundo sobre papel claro**, con la precisión de un
instrumento de laboratorio. El verde de marca manda en la navegación y en la
acción principal; el esmeralda queda reservado para lo que debe saltar a la
vista. La interfaz usa Fraunces Variable para identidad, IBM Plex Sans para
lectura y controles, e IBM Plex Mono para mediciones, unidades e
identificadores. Las fuentes se empaquetan como WOFF2 desde dependencias
bloqueadas de Fontsource, por lo que la PWA no solicita fuentes externas.

Los colores, escalas tipográficas, espacios, radios, sombras, movimiento,
anchos y capas viven en `src/shared/styles/tokens.css`. **Ningún color se
escribe a mano fuera de ese archivo**, incluidas las superficies oscuras —barra
lateral, panel de acceso, panel de simulación— y el texto que va encima, que
tienen token propio. Esa regla no es de estilo: un color suelto en una hoja de
componente sobrevive a un cambio de paleta y deja una pantalla con la identidad
anterior sin que nadie lo note.

El contraste de texto se mantiene con tinta oscura sobre superficies claras;
estados y orígenes añaden texto a la señal cromática, lo que importa más aquí
que con una marca cálida, porque el verde de marca y el verde de «conforme»
conviven en la misma pantalla. El foco visible usa un azul independiente de la
marca: sobre una interfaz verde, un contorno verde se confunde con el estado
seleccionado.

Las superficies principales tienen una envolvente y un núcleo concéntrico. Este
doble borde distingue contenedores sin recurrir a líneas grises planas. La
superficie se declara además como contenedor, de modo que lo que vive dentro se
adapta al ancho real de la tarjeta y no al de la ventana: una misma lista de
definiciones ocupa media retícula en una vista y una columna estrecha en otra, y
una consulta de medios no distingue esos dos casos.

La navegación lateral flota en escritorio y se convierte en una isla inferior en
pantallas estrechas. La isla muestra cuatro destinos —el recorrido diario— y
despliega el resto desde «Más»: once no caben en el ancho de un teléfono, y
reducirlos sin desplegable dejaría inalcanzables las secciones que no reciben
enlaces desde ninguna página. Si el destino actual está entre los desplegados,
el botón queda marcado. Los objetivos táctiles miden al menos 44 CSS px y se
respetan `safe-area-inset-*` y `100dvh`.

`DataTable<T>` define semántica, columnas, orden, selección, carga, vacío y error.
En móvil genera registros apilados con las mismas etiquetas. Ninguna tabla de
dominio reimplementa encabezados o selección. Las columnas pueden señalar qué
datos secundarios omitir en la tarjeta, pero origen, unidades y estado crítico
permanecen visibles.

Los componentes cubren acciones, campos, selección, capas, retroalimentación,
navegación, datos y vocabulario del dominio. Storybook documenta el inventario
esencial y ejecuta accesibilidad como error. Todo movimiento usa transformaciones
y opacidad, y `prefers-reduced-motion` reduce la duración global.
