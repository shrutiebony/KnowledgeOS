# Multi-stage C++ build. Java/Maven is legacy and is not used.
FROM debian:bookworm-slim AS build
WORKDIR /src

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ca-certificates libssl-dev \
    && rm -rf /var/lib/apt/lists/*

COPY CMakeLists.txt ./
COPY src_cpp ./src_cpp
COPY third_party ./third_party
COPY web ./web

RUN cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

FROM debian:bookworm-slim
WORKDIR /app

RUN apt-get update && apt-get install -y --no-install-recommends \
    libssl3 ca-certificates \
    && rm -rf /var/lib/apt/lists/* \
    && mkdir -p /data

COPY --from=build /src/build/knowledgeos /app/knowledgeos
COPY --from=build /src/web /app/web

ENV KNOWLEDGEOS_DB=/data/knowledgeos.db
ENV KNOWLEDGEOS_WEB=/app/web
EXPOSE 8080
VOLUME ["/data"]

ENTRYPOINT ["sh", "-c", "exec /app/knowledgeos"]
