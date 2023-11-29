import socket, struct, time, sys
import threading
import selectors
from collections import deque

import serial


def crc16(data, crc, poly=0x5935):
    cur = crc & 0xFFFF
    
    for c in data:
        j = 0x80
        while j > 0:
            bit = (cur & 0x8000) > 0
            if (c & j) > 0:
                bit = not bit
            cur <<= 1
            cur &= 0xFFFF
            if bit:
                cur ^= poly
            j >>= 1

    return cur


def open_udp(port):
    ret = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ret.bind(('0.0.0.0', port))
    ret.setblocking(False)
    return ret


TARGET_ADDRESS = '127.0.0.1'
PREAMBLE = bytes([0xCF, 0xEB, 0x01, 0x81])
class SerialProxy:
    def __init__(self, serial_port):
        self.serial_port = serial_port
        self._port_to_conn = {}
        self._remote_addr_to_port = {}
        self._port_to_remote_addr = {}
        
        self._selector = selectors.DefaultSelector()
        self._buffered_msg = bytearray()
        
        self._data_counter = 0
        self._loop_counter = 0
        self._checksum_fails_counter = 0
        self._packets_counter = 0
        self._stats_time = time.perf_counter_ns()
        
        self._running = True
    
    def _send_serial_packet(self, addr, local_port, remote_port, data):
        crc = 0
        header = struct.pack('<HBHH', len(data), addr, local_port, remote_port)
        crc = crc16(header, crc)
        crc = crc16(data, crc)
        
        b = bytearray()
        b += PREAMBLE
        b += header
        b += data
        b += struct.pack('<HB', crc, 10)
        self.serial_port.write(b)
    
    def _next_serial_packet(self):
        match_idx = 0
        while match_idx < 4:
            b = self.serial_port.read()
            if len(b) == 0:
                return None
            b = b[0]
            
            if b == PREAMBLE[match_idx]:
                match_idx += 1
                continue
            
            if match_idx > 0:
                self._buffered_msg.extend(PREAMBLE[:match_idx])
                match_idx = 0
            
            self._buffered_msg.append(b)
        
        crc = 0
        
        b = self.serial_port.read(7)
        crc = crc16(b, crc)
        length, addr, local_port, remote_port = struct.unpack('<HBHH', b)
        
        data = self.serial_port.read(length)
        crc = crc16(data, crc)
        
        self._data_counter += len(data)
        
        checksum, newline = struct.unpack('<HB', self.serial_port.read(3))
        
        if checksum != crc:
            return False
        
        return addr, local_port, remote_port, data
    
    def _send_loopback_packet(self, addr, target_port, remote_port, data):
        key = (addr, remote_port)
        if key not in self._remote_addr_to_port:
            local_port = max(remote_port, 10000)
            while local_port in self._port_to_conn:
                local_port += 1
                assert local_port < 65535
            
            print(f'Binding remote address {addr}:{remote_port} to {local_port}')
            sock = open_udp(local_port)
            self._selector.register(sock, selectors.EVENT_READ)
            
            self._remote_addr_to_port[key] = local_port
            assert local_port not in self._port_to_remote_addr
            self._port_to_remote_addr[local_port] = key
            assert local_port not in self._port_to_conn
            self._port_to_conn[local_port] = sock
        else:
            local_port = self._remote_addr_to_port[key]
            sock = self._port_to_conn[local_port]
        
        sock.sendto(data, (TARGET_ADDRESS, target_port))
    
    def _next_loopback_packets(self):
        if len(self._port_to_conn) == 0:
            return []
        
        ret = []
        for key, mask in self._selector.select(timeout=1.0):
            sock = key.fileobj
            try:
                data, addr = sock.recvfrom(1024)
            except ConnectionResetError:
                continue
            
            local_port = sock.getsockname()[1]
            target_port = addr[1]
            remote_addr, remote_port = self._port_to_remote_addr[local_port]
            ret.append((remote_addr, target_port, remote_port, data))
        return ret
    
    def inbound_loop(self):
        while self._running:
            self._loop_counter += 1
            apd = self._next_serial_packet()
            if apd is None:
                continue
            if apd is False:
                self._checksum_fails_counter += 1
                continue
            self._send_loopback_packet(*apd)
            self._packets_counter += 1
    
    def outbound_loop(self):
        while self._running:
            for apd in self._next_loopback_packets():
                self._send_serial_packet(*apd)
    
    def get_buffered_msg(self):
        ret = bytes(self._buffered_msg)
        self._buffered_msg.clear()
        return ret
    
    def get_stats(self):
        t = time.perf_counter_ns()
        dt = (t - self._stats_time) / 1e9
        self._stats_time = t
        
        bps = self._data_counter / dt
        self._data_counter = 0
        
        loop_time = dt / max(0.001, self._loop_counter)
        self._loop_counter = 0
        
        fails_per_sec = self._checksum_fails_counter / dt
        self._checksum_fails_counter = 0
        
        packets_per_sec = self._packets_counter / dt
        self._packets_counter = 0
        
        return {
            'Inbound bytes/sec': bps,
            'Inbound loop time': loop_time,
            'Inbound checksum fails/sec': fails_per_sec,
            'Inbound packets/sec': packets_per_sec
        }
    
    def close(self):
        self._running = False
        for sock in self._port_to_conn.values():
            self._selector.unregister(sock)
        time.sleep(1.5)
        for sock in self._port_to_conn.values():
            sock.close()
        self._port_to_conn.clear()
        self._remote_addr_to_port.clear()
        self._port_to_remote_addr.clear()


args = sys.argv[1:]
if len(args) < 1:
    print('Usage: python ./slime_ap.py <serial port>')
    exit()

port = args[0]

threads = []

ser = serial.Serial()
proxy = None
try:
    ser.port = port
    ser.baudrate = 115200 * 10
    ser.open()
    assert ser.is_open
    print('Serial open')
    
    proxy = SerialProxy(ser)
    
    threads.append(threading.Thread(name='Inbound', target=proxy.inbound_loop))
    threads.append(threading.Thread(name='Outbound', target=proxy.outbound_loop))
    
    for t in threads:
        t.start()
    print('Threads started')
    
    x = bytearray()
    while True:
        time.sleep(1.5)
        x += proxy.get_buffered_msg()
        while b'\n' in x:
            i = x.index(b'\n')
            print(repr(bytes(x[:i]))[2:-1])
            x = x[i+1:]
        
        stats = {'Open connections': len(proxy._port_to_conn)}
        stats.update(proxy.get_stats())
        
        print('; '.join(f'{k}: {v}' for k, v in stats.items()))
finally:
    print('Shutting down..')
    if proxy is not None:
        proxy.close()
        print('Closed proxy')
    ser.close()
    print('Closed serial')
    
    for t in threads:
        t.join()
    print('Threads joined')
