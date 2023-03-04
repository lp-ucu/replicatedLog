# replicatedLog
Distributed systems replicated log task. 

## General info
All components of the program are implemented in C++14. As HTTP server Crow library was used (included directly as crow_all.h file), and for RPC gRPC framework was chosen.

Executable file `main` implements both Master and Secondary when started with an appropriate flag (see instructions below).

### Basic features and implementation details:
1. **HTTP server** implements following methods:
  - Master:
     - `GET /messages` - retrieves all messages saved on the node
     - `GET /health` - retrieves the status of all secondaries (possible values: `"Healthy"`, `"Suspected"`, `"Unhealthy"`, `"Undefined"` (in case of some error)
     - `POST /message` (arguments: `{"message" : string, "w" : uint}`, where `message` is the message itself; `w` is a write concern, allowed values: [1,3]) - save message on Master and secondaries. If write concern `w` > 1, POST request will be blocked until the message is replicated on all secondaries specified by `w`.
  - Secondary:
     - `GET /messages` - retrieves all messages before the detected inconsistency is saved on the node (i.e., if Secondary contains messages with id 1,2,4 GET method will return only 1st and 2nd message).
2. **Replication logic**. Master asynchronously replicates all received messages to Secondaries using async gRPC.
3. **Sequential ordering and deduplication**: all messages are stored in the correct order without duplication.
4. **Health check**: Health monitor periodically (every 5 seconds) checks each Secondary's health. If it didn't receive responce for the 1st time, Secondary is claimed to be `"Suspected"`. If a responce is not received 5 times in a row, Secondary is claimed to be `"Unhealthy"`.
(gRPC `getLastMessageId()` is used to check health of the Secondary periodically)
5. **Retries**: in case of a missing message on Secondaries, Master implements retries every 5 seconds to recover this message. This logic is implemented based on Health monitor: when the monitor detects that the last message id on Secondary differs from the last message id on Master, it triggers Master to replicate this last missing message.  
Note: retry is performed only for 1 message. If Secondary is missing, for example, 10 messages, Master will require 10\*5 seconds to replicate them (for Health monitor to check last message of Secondary 10 times).
6. **Docker** (not implemented for now)

### Build instructions
#### Prerequirements:
**Linux**:  
```sudo apt-get install asio```

**MacOS**:  
```brew install asio```

install grpc: https://github.com/grpc/grpc/blob/v1.52.0/src/cpp/README.md

#### Installation:
**Linux**:
```
cmake -Bbuild
make --build build
```

**MacOS:**
```
mkdir build
cd build
cmake ..
make
```

### Execute instructions (on host):
**2 Secondaries:**  
```
cd build/src
./main -s --grpc-port 50051 --hostname 127.0.0.1 --http-port 28080
./main -s --grpc-port 50052 --hostname 127.0.0.1 --http-port 38080
```

**Master:**  
```
cd build/src
./main -m --hostname 127.0.0.1 --grpc-port 50050 --http-port 18080 -S 127.0.0.1:50051 -S 127.0.0.1:50052
```

## Test scenarios:
Note: we tested our system only with 1 Master and 2 Secondaries, though it should work with 3+ Secondaries as well.

### Prerequirements:
To send HTTP requests on MacOS/Linux/Windows(PowerShell) `curl` util can be used *(make sure to change URL/IP and port to one configured on Master)*:
- send POST request 
  ```
  send() { curl -d '{"message" : "'"$1"'", "w" : '"$2"'}' -H "Content-Type: application/json" -X POST http://localhost:18080/message; }
  ```
  usage: `send msg1 2` # will send message `"msg1"` with write concern `2`
- ```
  get_sec2() { curl -H "Content-Type: application/json" -X GET http://localhost:38080/messages; }
  ```
  usage: `get_sec2`
Alternatively Postman can be used to send HTTP requests.

#### Scenario 1 (from task description):
1. Start master + S1
    ```
    ./main -m --hostname 127.0.0.1 --grpc-port 50050 --http-port 18080 -S 127.0.0.1:50051 -S 127.0.0.1:50052
    ./main -s --grpc-port 50051 --hostname 127.0.0.1 --http-port 28080
    ```
    =>> Logs on Master (repeated every 5 sec):
    ```
    (2023-02-17 12:07:06) [INFO    ] Secondary '127.0.0.1:50051' is alive! Last consecutive message id: 0; Last id on master: 0
    (2023-02-17 12:07:06) [INFO    ] 14: failed to connect to all addresses; last error: UNKNOWN: ipv4:127.0.0.1:50052: Failed to connect to remote host:   Connection refused
    ```
2. send (Msg1, W=1)
    ```
    send Msg1 1
    ```
    ==> Responce is OK  
    ==> Logs on Master:  
    ```
    (2023-02-17 12:56:58) [INFO    ] Request: 127.0.0.1:56356 0x12a808800 HTTP/1.1 POST /message
    (2023-02-17 12:56:58) [INFO    ] received POST with message Msg1 and wite concern: 1
    (2023-02-17 12:56:58) [INFO    ] saveMessage 'Msg1' with id: 1
    (2023-02-17 12:56:58) [INFO    ]  Message replicate result:OK
    (2023-02-17 12:56:58) [INFO    ] Response: 0x12a808800 /message 201 0
    (2023-02-17 12:56:59) [INFO    ]  Syncing message [1. 'Msg1'] with 127.0.0.1:50051;
    (2023-02-17 12:56:59) [INFO    ]    Message sync result: OK  
    ```
    ==> Logs on Secondary:  
    ```
    (2023-02-17 12:56:59) [INFO    ] saveMessage 'Msg1' with id: 1
    ```
3. send (Msg2, W=2)  
    ```
    send Msg2 2
    ```
    ==> Responce is OK  
    ==> Logs on Master:  
    ```
    (2023-02-17 12:57:02) [INFO    ] Request: 127.0.0.1:56363 0x12a809c00 HTTP/1.1 POST /message
    (2023-02-17 12:57:02) [INFO    ] received POST with message Msg2 and wite concern: 2
    (2023-02-17 12:57:02) [INFO    ] saveMessage 'Msg2' with id: 2
    (2023-02-17 12:57:02) [INFO    ]  Message replicate result:OK
    (2023-02-17 12:57:02) [INFO    ] Response: 0x12a809c00 /message 201 0
    (2023-02-17 12:57:04) [INFO    ] Secondary '127.0.0.1:50051' is alive! Last consecutive message id: 2; Last id on master: 2
    (2023-02-17 12:57:04) [INFO    ] 14: failed to connect to all addresses; last error: UNKNOWN: ipv4:127.0.0.1:50052: Failed to connect to remote host:  Connection refused
    ```
    *(last 2 logs are logs from Health monitor)*  
    ==> Logs on Secondary 1:  
    ```
    (2023-02-17 12:38:29) [INFO    ] saveMessage 'msgMsg2' with id: 2
    ```
4. send (Msg3, W=3)  
    ```
    send Msg3 3
    ```
    ==> Call is blocked  
    ==> Logs on Master  
    ```
    (2023-02-17 12:57:07) [INFO    ] Request: 127.0.0.1:56369 0x12c808200 HTTP/1.1 POST /message
    (2023-02-17 12:57:07) [INFO    ] received POST with message Msg3 and wite concern: 3
    (2023-02-17 12:57:07) [INFO    ] saveMessage 'Msg3' with id: 3
    (2023-02-17 12:57:09) [INFO    ] Secondary '127.0.0.1:50051' is alive! Last consecutive message id: 3; Last id on master: 3
    (2023-02-17 12:57:09) [INFO    ] 14: failed to connect to all addresses; last error: UNKNOWN: ipv4:127.0.0.1:50052: Failed to connect to remote host: Connection refused
    (2023-02-17 12:57:14) [INFO    ] Secondary '127.0.0.1:50051' is alive! Last consecutive message id: 3; Last id on master: 3
    (2023-02-17 12:57:14) [INFO    ] 14: failed to connect to all addresses; last error: UNKNOWN: ipv4:127.0.0.1:50052: Failed to connect to remote host: Connection refused
    ```
    *(last 4 logs are logs from Health monitor)*  

    ==> Logs on Secondary 1:  
    ```
    (2023-02-17 12:57:07) [INFO    ] saveMessage 'Msg3' with id: 3
    ```
5. send (Msg4, W=1)  
    ```
    send Msg4 1
    ```
    ==> Responce is OK  
    ==> Logs on Master:
    ```
    (2023-02-17 12:57:15) [INFO    ] Request: 127.0.0.1:57328 0x12c810000 HTTP/1.1 POST /message
    (2023-02-17 12:57:15) [INFO    ] received POST with message Msg4 and wite concern: 1
    (2023-02-17 12:57:15) [INFO    ] saveMessage 'Msg4' with id: 4
    (2023-02-17 12:57:15) [INFO    ]  Message replicate result:OK
    (2023-02-17 12:57:15) [INFO    ] Response: 0x12c810000 /message 201 0
    ```
    ==> Logs on Secondary 1:
    ```
    (2023-02-17 12:57:15) [INFO    ] saveMessage 'Msg4' with id: 4
    ```
6. Start S2  
    ```
    ./main -s --grpc-port 50052 --hostname 127.0.0.1 --http-port 38080
    ```
    ==> Logs on Master:  
    *(Note Responce 0x12c808200 is made for Request Msg3, W=3)* 
    ```
    (2023-02-17 12:57:18) [INFO    ]  Message replicate result:OK
    (2023-02-17 12:57:18) [INFO    ] Response: 0x12c808200 /message 201 0
    (2023-02-17 12:57:19) [INFO    ] Secondary '127.0.0.1:50051' is alive! Last consecutive message id: 4; Last id on master: 4
    (2023-02-17 12:57:19) [INFO    ] Secondary '127.0.0.1:50052' is alive! Last consecutive message id: 0; Last id on master: 4
    (2023-02-17 12:57:19) [INFO    ] Status or consistency of the node: 127.0.0.1:50052 has changed
    (2023-02-17 12:57:19) [INFO    ] Status of secondary 127.0.0.1:50052 has changed to 0 last id: 0
    (2023-02-17 12:57:19) [INFO    ]  Syncing message [1. 'Msg1'] with 127.0.0.1:50052;
    (2023-02-17 12:57:19) [INFO    ]    Message sync result: OK
    (2023-02-17 12:57:24) [INFO    ] Secondary '127.0.0.1:50051' is alive! Last consecutive message id: 4; Last id on master: 4
    (2023-02-17 12:57:24) [INFO    ] Secondary '127.0.0.1:50052' is alive! Last consecutive message id: 1; Last id on master: 4
    (2023-02-17 12:57:24) [INFO    ] Status or consistency of the node: 127.0.0.1:50052 has changed
    (2023-02-17 12:57:24) [INFO    ] Status of secondary 127.0.0.1:50052 has changed to 0 last id: 1
    (2023-02-17 12:57:24) [INFO    ]  Syncing message [2. 'Msg2'] with 127.0.0.1:50052;
    (2023-02-17 12:57:24) [INFO    ]    Message sync result: OK
    (2023-02-17 12:57:29) [INFO    ] Secondary '127.0.0.1:50051' is alive! Last consecutive message id: 4; Last id on master: 4
    (2023-02-17 12:57:29) [INFO    ] Secondary '127.0.0.1:50052' is alive! Last consecutive message id: 3; Last id on master: 4
    (2023-02-17 12:57:29) [INFO    ] Status or consistency of the node: 127.0.0.1:50052 has changed
    (2023-02-17 12:57:29) [INFO    ] Status of secondary 127.0.0.1:50052 has changed to 0 last id: 3
    (2023-02-17 12:57:29) [INFO    ]  Syncing message [4. 'Msg4'] with 127.0.0.1:50052;
    (2023-02-17 12:57:29) [INFO    ]    Message sync result: OK
    (2023-02-17 12:57:34) [INFO    ] Secondary '127.0.0.1:50051' is alive! Last consecutive message id: 4; Last id on master: 4
    (2023-02-17 12:57:34) [INFO    ] Secondary '127.0.0.1:50052' is alive! Last consecutive message id: 4; Last id on master: 4  
    ```
    ==> Logs on Secondary 2:  
    ```
    (2023-02-17 12:57:18) [INFO    ] saveMessage 'Msg3' with id: 3
    (2023-02-17 12:57:19) [INFO    ] saveMessage 'Msg1' with id: 1
    (2023-02-17 12:57:24) [INFO    ] saveMessage 'Msg2' with id: 2
    (2023-02-17 12:57:29) [INFO    ] saveMessage 'Msg4' with id: 4
    ```
7. Check messages on S2 
    ```
    get_sec2
    ```
    ==> ["Msg1","Msg2","Msg3","Msg4"]


#### Scenario 2:
1. Start master + S1
2. send (Msg1, W=1) - Ok
3. send (Msg2, W=2) - Ok
4. send (Msg3, W=3) - Wait
5. Start S2
6. Kill S1
7. check health status - ["suspected", "healthy"]
8. Wait 30 sec
9. Check health status - ["unhealthy", "healthy"]
10. Start S1
11. Wait 20 sec
12. Check messages on S1 and S2 - [Msg1, Msg2, Msg3]

#### Scenario 3:
1. Start master + S1
2. send (Msg1, W=1) - Ok
3. send (Msg2, W=2) - Ok
4. Suspend S1 (or drop network btw S1 and master)
5. send (Msg3, W=1) - Ok
6. send (Msg4, W=1) - Ok
7. Recover S1
8. Check messages on S1 - [Msg1, Msg2, Msg3, Msg4]
