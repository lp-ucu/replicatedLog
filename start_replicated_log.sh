#!/bin/bash

# TODO: Add possibility to choose number of secondaries to start
if [ "$MODE" = "master" ]; then
  ./main -m --hostname "$HOSTNAME" --grpc-port "$GRPC_PORT" --http-port "$HTTP_PORT" -S $SECONDARY_1 -S $SECONDARY_2
elif [ "$MODE" = "secondary" ]; then
  ./main -s --grpc-port "$GRPC_PORT" --hostname "$HOSTNAME" --http-port "$HTTP_PORT"
else
  echo "Invalid MODE specified. Please set MODE to 'master' or 'secondary'."
  exit 1
fi
