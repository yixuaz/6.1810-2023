import struct

def calculate_checksum(data):
    if len(data) % 2 != 0:
        data += b'\0'

    checksum = 0
    for i in range(0, len(data), 2):
        w = (data[i] << 8) + data[i + 1]
        checksum += w
        checksum &= 0xffffffff  # Keep in 32 bits

    # Add carries
    checksum = (checksum >> 16) + (checksum & 0xffff)    
    checksum += checksum >> 16

    # One's complement and return
    checksum = ~checksum
    return checksum & 0xffff

def validate_tcp_checksum(packet):
    # Extract IP header length and total length
    ip_header_length = (packet[14] & 0x0F) * 4
    total_length = struct.unpack('!H', packet[16:18])[0]

    # Extract TCP segment
    tcp_segment = packet[14 + ip_header_length:14 + total_length]

    # Create pseudo-header
    source_ip = packet[26:30]
    destination_ip = packet[30:34]
    reserved = 0
    protocol = 6
    tcp_length = total_length - ip_header_length
    pseudo_header = struct.pack('!4s4sBBH', source_ip, destination_ip, reserved, protocol, tcp_length)

    # Extract received checksum
    received_checksum = struct.unpack('!H', tcp_segment[16:18])[0]
    print(received_checksum)

    # Zero out checksum field in TCP segment for calculation
    tcp_segment = tcp_segment[:16] + b'\x00\x00' + tcp_segment[18:]

    # Calculate checksum
    computed_checksum = calculate_checksum(pseudo_header + tcp_segment)

    
    
    # Check if checksums match
    if computed_checksum == received_checksum:
        return -received_checksum
    else:
        return computed_checksum

# Example packet (hex string)
packet_hex = "52550a00020252540012345608004500003b0000000064063ead0a00020f0a00020207d065f3000041a70000fa025018ffffee4e000061206d6573736167652066726f6d2078763621"
packet = bytes.fromhex(packet_hex)

# Validate checksum
result = validate_tcp_checksum(packet)
print(result)
