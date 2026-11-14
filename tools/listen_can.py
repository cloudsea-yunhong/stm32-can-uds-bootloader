import can, time
bus = can.Bus(interface="pcan", channel="PCAN_USBBUS1", bitrate=500000)
print("listening...")
end = time.time() + 8
while time.time() < end:
    m = bus.recv(timeout=0.5)
    if m:
        print(f"RX 0x{m.arbitration_id:03X}: {m.data.hex(' ')}")
bus.shutdown()
