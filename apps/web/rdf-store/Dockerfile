# Almacén RDF del sistema: Apache Jena Fuseki oficial sobre una base multiarquitectura.
# Reproduce la configuración TDB2 del despliegue actual sin extensiones de terceros.
FROM eclipse-temurin:21-jre-alpine

ARG FUSEKI_VERSION=5.5.0
ARG FUSEKI_SHA512=bfbf59eac731b71bcf8e148f2abeda9b4adca215639eef3bba61243b308572b23a9a70a43c1483247f9894bfb23d69b59e0e64e1da47cb3cfc592aa979084d5c

ENV FUSEKI_HOME=/jena-fuseki \
    FUSEKI_BASE=/fuseki

RUN apk add --no-cache bash curl \
    && curl -fSL "https://archive.apache.org/dist/jena/binaries/apache-jena-fuseki-${FUSEKI_VERSION}.tar.gz" -o /tmp/fuseki.tar.gz \
    && echo "${FUSEKI_SHA512}  /tmp/fuseki.tar.gz" | sha512sum -c - \
    && mkdir -p "${FUSEKI_HOME}" \
    && tar -xzf /tmp/fuseki.tar.gz -C "${FUSEKI_HOME}" --strip-components=1 \
    && rm /tmp/fuseki.tar.gz \
    && mkdir -p "${FUSEKI_BASE}/configuration" "${FUSEKI_BASE}/databases" \
    && addgroup -g 10001 fuseki \
    && adduser -u 10001 -G fuseki -h "${FUSEKI_BASE}" -D fuseki \
    && chown -R fuseki:fuseki "${FUSEKI_BASE}" "${FUSEKI_HOME}"

COPY espresso.ttl ${FUSEKI_BASE}/configuration/espresso.ttl
COPY shiro.ini ${FUSEKI_HOME}/shiro.ini
COPY entrypoint.sh /entrypoint.sh

RUN chown fuseki:fuseki "${FUSEKI_BASE}/configuration/espresso.ttl" "${FUSEKI_HOME}/shiro.ini" && chmod +x /entrypoint.sh

USER fuseki
WORKDIR ${FUSEKI_BASE}
EXPOSE 3030
ENTRYPOINT ["/entrypoint.sh"]
CMD ["java", "-jar", "/jena-fuseki/fuseki-server.jar"]
