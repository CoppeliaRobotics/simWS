# WebSocket plugin for CoppeliaSim

### Compiling

1. Install required packages for simStubsGen: see simStubsGen's [README](https://github.com/CoppeliaRobotics/include/blob/master/simStubsGen/README.md)
2. Checkout, compile and install into CoppeliaSim:
```sh
$ git clone https://github.com/CoppeliaRobotics/simExtWS.git
$ cd simExtWS
$ git checkout coppeliasim-v4.5.0-rev0
$ mkdir build && cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ cmake --build .
$ cmake --install .
```

NOTE: replace `coppeliasim-v4.5.0-rev0` with the actual CoppeliaSim version you have.

### Usage

Use `simWS.start` to start listening on the specified port, `simWS.setMessageHandler` to set an handler for incoming messages, and `simWS.send` to send messages on the specified connection:

```lua
-- Simple echo server

function onMessage(server,connection,data)
    simWS.send(server,connection,data)
end

function sysCall_init()
    server=simWS.start(9000)
    simWS.setMessageHandler(server,'onMessage')
end
```

It is possible to broadcast a message to all connected clients by tracking active connections via open/close events:

```lua
-- Simple broadcaster

function onOpen(server,connection)
    clients[server]=clients[server] or {}
    clients[server][connection]=1
end

function onClose(server,connection)
    clients[server][connection]=nil
end

function broadcast(server,data)
    for connection,_ in pairs(clients[server] or {}) do
        simWS.send(server,connection,data)
    end
end

function sysCall_init()
    clients={}
    server=simWS.start(9000)
    simWS.setOpenHandler(server,'onOpen')
    simWS.setCloseHandler(server,'onClose')
end
```

See also the examples in the [`examples`](examples) subdirectory.
