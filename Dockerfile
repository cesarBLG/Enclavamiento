FROM docker.io/debian:latest
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && \
    apt-get install -y --no-install-recommends \
        git \
        g++ \
        make \
        cmake \
        libmosquitto-dev \
        mosquitto-clients \
        python3 \
        python-is-python3 \
        python3-paho-mqtt && \
    rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN cmake -S /app -B /app/build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build /app/build --parallel

WORKDIR /app/dist
RUN cp /app/build/enclavamiento . && \
    cp /app/build/gestor_conexion/gestor_conexion .

ENTRYPOINT ["./run.sh"]
CMD ["full"]
