FROM debian:12-slim AS build

# copy sources from host
ENV BUILD_PATH /usr/local/replicatedLog
RUN mkdir -p $BUILD_PATH
COPY . $BUILD_PATH

# install required for compilation libs
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
  libasio-dev \
  && apt-get clean

# get latest grpc available today
ENV GRPC_RELEASE_TAG v1.56.0

ENV  MY_INSTALL_DIR $HOME/.local
RUN mkdir -p $MY_INSTALL_DIR
RUN export PATH="$MY_INSTALL_DIR/bin:$PATH"

# clone and compile gRPC
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

# compile app
RUN cd $BUILD_PATH && \
    export CMAKE_PREFIX_PATH=$MY_INSTALL_DIR && \
    mkdir -p build && \
    cd build && \
    cmake .. && \
    make


FROM debian:12-slim AS runtime

ENV EXEC_PATH /usr/local/bin/
ENV BUILD_PATH /usr/local/replicatedLog

# copy executable and starting script
COPY --from=build $BUILD_PATH/build/src/main $EXEC_PATH
COPY start_replicated_log.sh $EXEC_PATH

WORKDIR $EXEC_PATH

CMD ["./start_replicated_log.sh"]
