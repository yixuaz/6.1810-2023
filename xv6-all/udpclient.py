import socket
import sys

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

server_addr = ('localhost', int(sys.argv[1])) 
message = b'This is the client!'
sock.sendto(message, server_addr)

buf, raddr = sock.recvfrom(4096)
print(buf.decode("utf-8"), file=sys.stderr)

sock.close()