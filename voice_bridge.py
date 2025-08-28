#!/usr/bin/env python3
"""
Voice bridge between RT-STT and C++ application.
Uses the official RT-STT Python client and communicates via stdout.
"""

import sys
import json
from rt_stt import RTSTTClient
import signal

client = None

def cleanup(signum, frame):
    """Clean shutdown on signal"""
    global client
    if client:
        try:
            client.stop_listening()
            client.disconnect()
        except:
            pass
    sys.exit(0)

def main():
    global client
    
    # Set up signal handlers
    signal.signal(signal.SIGINT, cleanup)
    signal.signal(signal.SIGTERM, cleanup)
    
    # Unbuffered output
    sys.stdout = sys.stdout.detach()
    sys.stdout = open(sys.stdout.fileno(), 'wb', 0)
    
    try:
        # Create and connect client
        client = RTSTTClient()
        client.connect()
        
        # Send ready message
        msg = json.dumps({"type": "ready", "status": "connected"}) + "\n"
        sys.stdout.write(msg.encode('utf-8'))
        sys.stdout.flush()
        
        # Set up transcription callback
        def on_transcription(result):
            msg = json.dumps({
                "type": "transcription",
                "text": result.text,
                "confidence": result.confidence,
                "language": getattr(result, 'language', 'en'),
                "is_final": getattr(result, 'is_final', True)
            }) + "\n"
            sys.stdout.write(msg.encode('utf-8'))
            sys.stdout.flush()
        
        def on_error(error):
            msg = json.dumps({"type": "error", "error": str(error)}) + "\n"
            sys.stdout.write(msg.encode('utf-8'))
            sys.stdout.flush()
        
        # Register callbacks
        client.on_transcription(on_transcription)
        client.on_error(on_error)
        
        # Subscribe and start listening
        client.subscribe()
        client.start_listening()
        
        msg = json.dumps({"type": "ready", "status": "listening"}) + "\n"
        sys.stdout.write(msg.encode('utf-8'))
        sys.stdout.flush()
        
        # Keep running
        import select
        while True:
            try:
                # Use select instead of signal.pause() for better cross-platform compatibility
                select.select([], [], [])
            except:
                pass
            
    except Exception as e:
        msg = json.dumps({"type": "error", "error": str(e)}) + "\n"
        sys.stdout.write(msg.encode('utf-8'))
        sys.stdout.flush()
        cleanup(None, None)

if __name__ == "__main__":
    main()
