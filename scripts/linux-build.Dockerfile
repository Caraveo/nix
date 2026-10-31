FROM debian:stable
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential bc bison flex libelf-dev libssl-dev \
    && rm -rf /var/lib/apt/lists/*
