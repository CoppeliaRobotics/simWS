import asyncio
import websockets

async def hello():
    async with websockets.connect("ws://localhost:9000") as websocket:
        payload = 'Hello, world!'
        print('Sending:', payload)
        await websocket.send(payload)
        payload = await websocket.recv()
        print('Received:', payload)

asyncio.run(hello())
