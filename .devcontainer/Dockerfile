FROM ubuntu:22.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gcc \
    gdb \
    make \
    cmake \
    valgrind \
    strace \
    cu \
    minicom \
    picocom \
    screen \
    setserial \
    socat \
    uml-utilities \
    git \
    curl \
    wget \
    vim \
    nano \
    less \
    man-db \
    jq \
    unzip \
    sudo \
    ca-certificates \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y nodejs \
    && apt-get clean && rm -rf /var/lib/apt/lists/*

ARG USERNAME=dev
ARG USER_UID=1000
ARG USER_GID=1000

RUN groupadd --gid $USER_GID $USERNAME \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME \
    && echo "$USERNAME ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers

RUN usermod -aG dialout $USERNAME

RUN npm install -g @anthropic-ai/claude-code

RUN mkdir -p /workspace && chown $USERNAME:$USERNAME /workspace

USER $USERNAME
WORKDIR /workspace