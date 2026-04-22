FROM alpine:3.19 AS builder

RUN apk add --no-cache \
    build-base \
    cmake \
    git \
    linux-headers \
    ca-certificates \
    wget \
    tar \
    xz

WORKDIR /app

COPY CMakeLists.txt ./

RUN mkdir -p src && touch src/main.cpp && \
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

COPY src ./src

RUN cmake --build build --parallel

FROM alpine:3.19

RUN apk add --no-cache \
    libstdc++ \
    libgcc

WORKDIR /app

COPY --from=builder /app/build/fuzzy_service /usr/local/bin/fuzzy_service

EXPOSE 5000

CMD ["fuzzy_service"]