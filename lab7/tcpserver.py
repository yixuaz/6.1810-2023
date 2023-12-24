import socket
import sys


def start_tcp_server(port):
    # 创建 socket 对象
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # addr = ('172.16.100.1', port)
    addr = ('localhost', port)
    # addr = ('172.17.209.209', port)
    # 绑定 IP 地址和端口
    server_socket.bind(addr)

    # 开始监听端口，设置最大连接数
    server_socket.listen(5)
    print(f"listening on {addr}")

    # 接受客户端连接
    while True:
        client_socket, addr = server_socket.accept()
        print(f"Connection from {addr} has been established.")

        # 接收客户端发送的数据
        while True:
            data = client_socket.recv(1024)
            if not data:
                break
            print("Received from client:", data.decode())
            client_socket.send(data)

        # 关闭连接
        client_socket.close()
    server_socket.close()
    print("Connection closed.")

# 运行服务器
start_tcp_server(int(sys.argv[1]))