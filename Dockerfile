FROM ubuntu:24.04 AS build
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake pkg-config libssl-dev ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY CMakeLists.txt ./
COPY src_cpp ./src_cpp
COPY third_party ./third_party
COPY web ./web
COPY tools ./tools
COPY runtime_lambda ./runtime_lambda

RUN cmake -S . -B /src/build -DCMAKE_BUILD_TYPE=Release \
    && cmake --build /src/build --target knowledgeos -j"$(nproc)"

FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates libssl3 python3 python3-pip \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=build /src/build/knowledgeos /app/knowledgeos
COPY --from=build /src/web /app/web
COPY --from=build /src/tools /app/tools
COPY requirements.txt /app/requirements.txt
COPY deploy/docker/entrypoint.sh /app/entrypoint.sh
RUN sed -i 's/\r$//' /app/entrypoint.sh \
    && chmod +x /app/knowledgeos /app/entrypoint.sh \
    && python3 -m pip install --no-cache-dir --break-system-packages -r /app/requirements.txt \
    && mkdir -p /data

ENV PORT=8080 \
    KNOWLEDGEOS_DB=/data/knowledgeos.db \
    KNOWLEDGEOS_WEB=/app/web \
    KNOWLEDGEOS_WIKIPEDIA_CRAWL=false \
    KNOWLEDGEOS_GDELT_CRAWL=false

EXPOSE 8080
VOLUME ["/data"]
ENTRYPOINT ["/app/entrypoint.sh"]
