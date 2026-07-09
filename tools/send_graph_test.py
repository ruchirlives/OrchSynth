import socket
import sys
import os

def pad_string(s):
    # Convert to bytes and pad with 0s to a multiple of 4
    b = s.encode('utf-8')
    padding_len = 4 - (len(b) % 4)
    if padding_len == 0:
        padding_len = 4
    return b + (b'\x00' * padding_len)

def make_osc_string_message(address, arg_string):
    # OSC format: Address, Type tag, String Argument
    msg = pad_string(address)
    msg += pad_string(',s')
    msg += pad_string(arg_string)
    return msg

def make_osc_empty_message(address):
    # OSC format: Address, Type tag
    msg = pad_string(address)
    msg += pad_string(',')
    return msg

def send_osc(ip, port, data):
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.sendto(data, (ip, port))
    sock.close()

if __name__ == "__main__":
    ip = "127.0.0.1"
    port = 9020
    
    # Path to the JSON patch
    default_json = "../patches/saw_filter_gain.orchfaust.json"
    json_path = sys.argv[1] if len(sys.argv) > 1 else default_json
    
    if not os.path.exists(json_path):
        print(f"Error: File {json_path} not found.")
        sys.exit(1)
        
    with open(json_path, 'r') as f:
        json_content = f.read()
        
    print(f"Sending graph JSON from {json_path} to {ip}:{port}...")
    
    # Send graph load message
    msg_load = make_osc_string_message("/orch_faust/load_graph", json_content)
    send_osc(ip, port, msg_load)
    
    print("Graph JSON sent successfully.")
