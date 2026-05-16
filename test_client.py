import asyncio
import websockets
import sys

async def connect_and_query():
    uri = "ws://localhost:6969"
    try:
        async with websockets.connect(uri) as websocket:
            print(f"Connected to NanoVaultDb at {uri}")
            print("Type 'exit' or 'quit' to stop.\n")

            while True:
                # Get command from user
                query = input("nanoVaultDb> ")
                if query.lower() in ['exit', 'quit']:
                    break
                
                if not query.strip():
                    continue

                # Send multi-line queries if necessary or just the single line
                await websocket.send(query)

                # Receive and print response
                response = await websocket.recv()
                print(response)

    except ConnectionRefusedError:
        print("Error: Could not connect to the NanoVaultDb server. Is it running?")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        # One-shot mode
        query = " ".join(sys.argv[1:])
        asyncio.run(connect_and_query_oneshot(query))
    else:
        # Interactive mode
        asyncio.run(connect_and_query())

async def connect_and_query_oneshot(query):
    uri = "ws://localhost:6969"
    async with websockets.connect(uri) as websocket:
        await websocket.send(query)
        response = await websocket.recv()
        print(response)
