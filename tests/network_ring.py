"""Exercise DMA ring wrap with real Ethernet frames through QEMU's socket backend."""
import pathlib
import os
import socket
import struct
import subprocess
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
BUILD = ROOT / "build"
with socket.socket() as reserve:
    reserve.bind(("127.0.0.1", 0))
    port = reserve.getsockname()[1]
log = BUILD / "network-ring.log"
log.write_text("")
process = subprocess.Popen([
    "qemu-system-x86_64", "-m", "64M", "-vga", "std",
    "-drive", f"format=raw,file={BUILD / os.environ.get('POLLIK_TEST_IMAGE', 'PollikOS-Alpha.img')},if=ide,index=0",
    "-display", "none", "-serial", f"file:{log}",
    "-netdev", f"socket,id=n,listen=127.0.0.1:{port}", "-device", "rtl8139,netdev=n",
], creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
try:
    deadline = time.monotonic() + 15
    while "desktop ready" not in log.read_text():
        assert time.monotonic() < deadline, log.read_text()
        time.sleep(.1)
    connection = socket.create_connection(("127.0.0.1", port), timeout=4)

    def read_exact(n):
        result = b""
        while len(result) < n:
            part = connection.recv(n - len(result))
            assert part, "QEMU closed Ethernet connection"
            result += part
        return result

    connection.settimeout(5)
    # Obtain a real DHCP lease on this isolated Ethernet segment first.
    # Use a non-QEMU-default subnet to reject hidden static configuration.
    target_ip = bytes([192, 0, 2, 25])
    server_ip = bytes([192, 0, 2, 1])
    source_mac = bytes.fromhex("525400123456")
    def checksum(b):
        if len(b)%2:b+=b"\0"
        v=sum(struct.unpack("!"+"H"*(len(b)//2),b))
        while v>>16:v=(v&65535)+(v>>16)
        return (~v)&65535
    for message_type in (2,5):
        while True:
            frame=read_exact(struct.unpack("!I",read_exact(4))[0])
            if len(frame)>=282 and frame[12:14]==b"\x08\x00" and frame[23]==17 and frame[34:38]==struct.pack("!HH",68,67):break
        bootp=bytearray(240)
        bootp[0:3]=bytes([2,1,6]);bootp[4:8]=frame[46:50]
        bootp[16:20]=target_ip;bootp[28:34]=frame[70:76];bootp[236:240]=bytes.fromhex("63825363")
        options=bytes([53,1,message_type,54,4])+server_ip+bytes([1,4,255,255,255,0,3,4])+server_ip+bytes([6,4])+server_ip+bytes([51,4])+struct.pack("!I",3600)+bytes([255])
        payload=bootp+options
        udp=struct.pack("!HHHH",67,68,8+len(payload),0)+payload
        ip=bytearray(struct.pack("!BBHHHBBH4s4s",69,0,20+len(udp),1,0,64,17,0,server_ip,bytes([255])*4))
        ip[10:12]=struct.pack("!H",checksum(ip))
        reply=bytes([255])*6+source_mac+b"\x08\x00"+ip+udp
        connection.sendall(struct.pack("!I",len(reply))+reply)
    deadline=time.monotonic()+5
    while "DHCP: Bound successfully, IP=192.0.2.25" not in log.read_text():
        assert time.monotonic()<deadline,log.read_text()
        time.sleep(.05)

    source_mac = bytes.fromhex("525400123456")
    source_ip = server_ip

    frame = (bytes([255]) * 6 + source_mac + bytes.fromhex("0806") +
             struct.pack("!HHBBH", 1, 0x800, 6, 4, 1) + source_mac + source_ip +
             bytes(6) + target_ip).ljust(60, b"\0")
    # Each receive consumes 68 bytes including status, CRC and alignment.
    # 300 frames cross the 8 KiB DMA ring boundary twice.
    for i in range(300):
        connection.sendall(struct.pack("!I", len(frame)) + frame)
        size = struct.unpack("!I", read_exact(4))[0]
        assert 42 <= size <= 1536, size
        reply = read_exact(size)
        assert reply[:6] == source_mac and reply[12:14] == b"\x08\x06", (i, reply)
        assert reply[20:22] == b"\x00\x02" and reply[28:32] == target_ip, (i, reply)
        assert reply[32:38] == source_mac and reply[38:42] == source_ip, (i, reply)
    assert "KERNEL EXCEPTION" not in log.read_text()
    print("PASS: 300 Ethernet ARP requests/replies, including two DMA ring wraps")
finally:
    process.terminate()
    process.wait(timeout=5)
