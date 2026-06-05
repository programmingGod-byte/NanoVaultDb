import asyncio
import websockets

async def receive_signals():
    uri = "ws://localhost:4900"
    print(f"Connecting to {uri}...")
    
    try:
        async with websockets.connect(uri) as websocket:
            print("Connected! Waiting for strategy signals...")
            ct=0;
            
            while True:
                # Receive message from the C++ HFT engine
                message = await websocket.recv()
                
                # Parse the custom 12-character format
                # Example: "00020000True"
                if len(message) >= 8:
                    symbol = message[0:4]
                    strategy_id = message[4:8]
                    status = message[8:]
                    
                    print(f"--- Strategy Signal Received ---")
                    print(f"| Id: {ct}")
                    print(f"| Symbol Index:   {symbol}")
                    print(f"| Strategy Index: {strategy_id}")
                    print(f"| Signal Status:  {status}")
                    print(f"--------------------------------")
                    ct+=1
                else:
                    print(f"Received malformed message: {message}")

    except ConnectionRefusedError:
        print("Error: Could not connect. Is the HFT engine running?")
    except Exception as e:
        print(f"An error occurred: {e}")

if __name__ == "__main__":
    try:
        asyncio.run(receive_signals())
    except KeyboardInterrupt:
        print("\nDisconnected.")
