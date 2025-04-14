# Use an ARM64 Ubuntu base image
FROM ubuntu:22.04 AS build

# Set environment variables to non-interactive (useful for automated builds)
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    sudo \
    python3 \
    bash \
    locales \
    file \
    && rm -rf /var/lib/apt/lists/*

RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8

COPY poky-glibc-x86_64-core-image-minimal-cortexa76-raspberrypi5-toolchain-4.0.25.sh /tmp/sdk.sh
RUN chmod +x /tmp/sdk.sh && \
    /tmp/sdk.sh -d /opt/poky-sdk && \
    find /opt/poky-sdk -name "environment-setup-aarch64-poky-linux"

WORKDIR /app
COPY . .


# Configure and build your project
#RUN cmake -DCMAKE_BUILD_TYPE=Debug . && make
RUN bash -c "source $(find /opt/poky-sdk -name 'environment-setup-*' | head -n 1) && \
    cmake -DCMAKE_BUILD_TYPE=Debug . && \
    make -j$(nproc)"

# Stage 2: Export the final binary
FROM scratch AS export-stage
# Copy the final binary from the build stage.
# Adjust the path and binary name accordingly.
COPY --from=build /app/RPISignal /RPISignal