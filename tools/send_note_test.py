import socket
import struct
import time
import sys

def pad_string(s):
    b = s.encode('utf-8')
    padding_len = 4 - (len(b) % 4)
    if padding_len == 0:
        padding_len = 4
    return b + (b'\x00' * padding_len)

def make_osc_note_on(pitch, velocity):
    msg = pad_string("/orch_faust/note_on")
    msg += pad_string(",ff")
    msg += struct.pack(">f", float(pitch))
    msg += struct.pack(">f", float(velocity))
    return msg

def make_osc_note_off(pitch):
    msg = pad_string("/orch_faust/note_off")
    msg += pad_string(",f")
    msg += struct.pack(">f", float(pitch))
    return msg

def make_osc_param(node_id, param, value):
    msg = pad_string("/orch_faust/set_param")
    msg += pad_string(",ssf")
    msg += pad_string(node_id)
    msg += pad_string(param)
    msg += struct.pack(">f", float(value))
    return msg

def send_osc(ip, port, data):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(data, (ip, port))
    sock.close()

if __name__ == "__main__":
    ip = "127.0.0.1"
    port = 9020
    
    print("Testing notes over OSC on port 9020...")
    
    # Send Note On (C4, MIDI 60, full velocity)
    print("Note ON: C4")
    send_osc(ip, port, make_osc_note_on(60, 1.0))
    time.sleep(1.0)
    
    # Modulate filter cutoff dynamically
    print("Modulating filter1 cutoff to 500 Hz...")
    send_osc(ip, port, make_osc_param("filter1", "cutoff", 500.0))
    time.sleep(0.5)
    
    print("Modulating filter1 cutoff to 3000 Hz...")
    send_osc(ip, port, make_osc_param("filter1", "cutoff", 3000.0))
    time.sleep(0.5)

    print("Note OFF: C4")
    send_osc(ip, port, make_osc_note_off(60))
    time.sleep(0.5)
    
    # Play chord (C4, E4, G4)
    print("Chord ON: C4 + E4 + G4")
    send_osc(ip, port, make_osc_note_on(60, 0.7))
    send_osc(ip, port, make_osc_note_on(64, 0.7))
    send_osc(ip, port, make_osc_note_on(67, 0.7))
    time.sleep(1.5)
    
    print("Chord OFF")
    send_osc(ip, port, make_osc_note_off(60))
    send_osc(ip, port, make_osc_note_off(64))
    send_osc(ip, port, make_osc_note_off(67))
    
    print("Test complete.")
