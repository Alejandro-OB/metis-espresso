# Almacén RDF

Imagen del almacén RDF de la plataforma: **Apache Jena Fuseki 5.5.0** oficial
sobre `eclipse-temurin:21-jre-alpine`, sin extensiones de terceros.

## Por qué una imagen propia

Las imágenes publicadas de Fuseki que se evaluaron no sirven en la Raspberry Pi:

| Imagen | Impedimento |
|---|---|
| `stain/jena-fuseki` | Solo publica `linux/amd64`. |
| `secoresearch/fuseki` | Publica `arm64`, pero usa TDB1, añade un índice Lucene, carga clases propias y llama `ds` al conjunto de datos: habría que sustituir toda su configuración. |

`eclipse-temurin` publica `arm64` y el paquete de Fuseki es Java puro, así que la
misma imagen sirve en x86_64 y en aarch64.

## Contenido

| Archivo | Función |
|---|---|
| `Dockerfile` | Descarga el paquete oficial del archivo de Apache y verifica su SHA-512. |
| `espresso.ttl` | Conjunto de datos `espresso` sobre TDB2 en `/fuseki/databases/espresso`, con los extremos `query`, `sparql`, `update`, `data` y `get`. |
| `shiro.ini` | Autenticación: la administración (`/$/…`) pide contraseña; `/$/ping`, `/$/status` y `/$/metrics` quedan abiertos para la comprobación de salud. |
| `entrypoint.sh` | Copia `shiro.ini` a `FUSEKI_BASE` en el primer arranque y aplica `ADMIN_PASSWORD`. |

Los extremos del conjunto de datos no piden autenticación. Por eso
`compose.yaml` no publica el puerto 3030 fuera de la red interna de la
composición.

## Memoria

El límite de memoria de la JVM se fija desde `compose.yaml` con
`JDK_JAVA_OPTIONS` (variable `RDF_JAVA_OPTIONS`, por omisión `-Xmx512m`). Sin ese
límite la JVM lo deduce del hardware y el consumo cambia de un equipo a otro.

## Construcción

La imagen no compila nada: descarga, verifica y extrae. Se puede construir
directamente en la Raspberry Pi o, de forma cruzada, en un equipo con emulación
`arm64` registrada. Desde `apps/web/`:

```bash
docker compose --profile semantic build rdf-store
```

## Usuario y volumen

El servicio corre como usuario sin privilegios (UID `10001`) y guarda los datos
en el volumen `rdf-data`, montado en `/fuseki`. Un volumen nuevo se inicializa con
la configuración de la imagen; un volumen creado por otra imagen que pertenezca a
`root` hay que reasignarlo a ese usuario antes de reutilizarlo.
