#FROM debian:11-slim
#FROM gcc:13.1
FROM ubuntu:latest

#gcc is based on Debian and is requred to build gRPC

ENV BUILD_PATH /usr/local/replicatedLog
RUN mkdir -p $BUILD_PATH

COPY . $BUILD_PATH

RUN apt-get update && apt-get install -y \
  autoconf \
  automake \
  build-essential \
  cmake \
  curl \
  g++ \
  git \
  libtool \
  make \
  pkg-config \
  unzip \
  wget \
  libasio-dev \
  && apt-get clean

# some ubuntu stuff
RUN apt-get update && apt-get install -y locales && rm -rf /var/lib/apt/lists/* \
	&& localedef -i en_US -c -f UTF-8 -A /usr/share/locale/locale.alias en_US.UTF-8
ENV LANG en_US.utf8

# get latest grpc available today
ENV GRPC_RELEASE_TAG v1.56.0

ENV  MY_INSTALL_DIR $HOME/.local
RUN mkdir -p $MY_INSTALL_DIR
RUN export PATH="$MY_INSTALL_DIR/bin:$PATH"

RUN git clone --recurse-submodules -b $GRPC_RELEASE_TAG --depth 1 --shallow-submodules https://github.com/grpc/grpc && \
    cd grpc && \
    mkdir -p cmake/build && \
    cd cmake/build && \
    export PATH="$MY_INSTALL_DIR/bin:$PATH" && \
    cmake -DgRPC_INSTALL=ON \
      -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_INSTALL_PREFIX=$MY_INSTALL_DIR \
      ../.. && \
    make -j 4 && \
    make install && \
    cd -

RUN cd $BUILD_PATH && \
    export CMAKE_PREFIX_PATH=$MY_INSTALL_DIR && \
    mkdir -p build && \
    cd build && \
    cmake .. && \
    make

# no need for /home/app/server.js because of WORKDIR
#CMD ["node", "server.js"]
CMD ["sleep", "infinity"]
