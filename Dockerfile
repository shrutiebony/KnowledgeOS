# Multi-stage build: Maven compiles the Spring Boot 3.3.5 fat JAR, then a
# slim Temurin 21 JRE image runs it. No secrets are copied or baked in.
FROM maven:3.9-eclipse-temurin-21 AS build
WORKDIR /src

COPY pom.xml .
RUN mvn -q -B -DskipTests dependency:go-offline

COPY src ./src
COPY tools ./tools
RUN mvn -q -B -DskipTests package \
    && cp target/knowledgeos-1.0.0.jar /src/app.jar

FROM eclipse-temurin:21-jre
WORKDIR /app

RUN mkdir -p /data

COPY --from=build /src/app.jar /app/app.jar
COPY --from=build /src/tools /app/tools

# H2 file store lives on the named volume. Override at runtime if needed.
# Do not put passwords, API keys, or credentials here.
ENV SPRING_DATASOURCE_URL="jdbc:h2:file:/data/knowledgeos;AUTO_SERVER=TRUE"
ENV JAVA_TOOL_OPTIONS="-XX:MaxRAMPercentage=75.0"

EXPOSE 8080
VOLUME ["/data"]

# Cloud Run / Render inject PORT. Local compose uses 8080.
# Bind all interfaces so the container is reachable.
ENTRYPOINT ["sh", "-c", "exec java -jar /app/app.jar --server.address=0.0.0.0 --server.port=${PORT:-${SERVER_PORT:-8080}}"]
