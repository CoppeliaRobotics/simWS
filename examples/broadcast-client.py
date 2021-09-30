import asyncio
import websockets

async def main():
    async with websockets.connect("ws://localhost:9000") as websocket:
        while True:
            payload = await websocket.recv()
            print('Received:', payload)

asyncio.run(main())
