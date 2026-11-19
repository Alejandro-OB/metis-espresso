# Entorno de construcción del artefacto del modelo (ADR-011 § 4.1).
#
# Existe para que el binario que produce la evidencia quede asociado a un
# procedimiento repetible por un tercero, y no al estado del equipo donde se
# compiló. Por eso la imagen base va fijada por versión: el compilador forma
# parte de la procedencia del artefacto.
#
# No instala la herramienta de modelado. El artefacto ya lleva sus 263 archivos
# de código C, de modo que aquí solo hace falta un compilador de C, como
# argumenta ADR-011 § 5.5.
FROM debian:trixie-20250811-slim

RUN apt-get update && apt-get install --yes --no-install-recommends \
        build-essential \
        cmake \
        curl \
        zip \
        unzip \
        ca-certificates \
    && rm -rf /var/lib/apt/lists/*

# SUNDIALS, solo para los artefactos que integran con CVODE (la FMU v2, porque su
# circuito hidráulico tiene una constante de 0,05 s y con el Euler de paso fijo
# la presión diverge). El artefacto lleva las cabeceras pero no la biblioteca: en
# x86_64 se enlazó estática desde la herramienta de modelado. Se compila aquí la
# misma versión que dicen sus cabeceras (sundials_config.h, 7.8.0), estática y
# con código reubicable, y se verifica el archivo por su sha256: una versión
# distinta podría compilar y comportarse diferente. Debian trae otra versión.
ARG SUNDIALS_VERSION=7.8.0
ARG SUNDIALS_SHA256=69ec92653e998e4841b59d363b3abf21299251991390f52917402737164ca574
RUN curl -sSL -o /tmp/sundials.tar.gz \
        https://github.com/LLNL/sundials/releases/download/v${SUNDIALS_VERSION}/sundials-${SUNDIALS_VERSION}.tar.gz \
    && echo "${SUNDIALS_SHA256}  /tmp/sundials.tar.gz" | sha256sum -c - \
    && tar xzf /tmp/sundials.tar.gz -C /tmp \
    && cmake -S /tmp/sundials-${SUNDIALS_VERSION} -B /tmp/sundials-build \
        -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/opt/sundials -DCMAKE_INSTALL_LIBDIR=lib \
        -DBUILD_SHARED_LIBS=OFF -DBUILD_STATIC_LIBS=ON \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DEXAMPLES_ENABLE_C=OFF \
    && cmake --build /tmp/sundials-build --parallel "$(nproc)" \
    && cmake --install /tmp/sundials-build \
    && rm -rf /tmp/sundials*

WORKDIR /trabajo
COPY recompilar-artefacto.sh /usr/local/bin/recompilar-artefacto
RUN chmod +x /usr/local/bin/recompilar-artefacto

ENTRYPOINT ["/usr/local/bin/recompilar-artefacto"]
