#!/usr/bin/env python3
import socket
import sys
import threading


def sender(sock, input_path):
    with open(input_path, "rb") as f:
        while True:
            chunk = f.read(1024)
            if not chunk:
                break
            sock.sendall(chunk)

    # Tell ZC702 that input stream finished
    sock.shutdown(socket.SHUT_WR)


def receiver(sock, output_path):
    total = 0
    with open(output_path, "wb") as f:
        while True:
            data = sock.recv(4096)
            if not data:
                break
            f.write(data)
            total += len(data)

    print(f"Received {total} bytes")


def main():
    if len(sys.argv) != 4:
        print("Usage:")
        print("  python3 lan_rle_client.py input.fa output.rle 192.168.2.10")
        sys.exit(1)

    input_path = sys.argv[1]
    output_path = sys.argv[2]
    ip = sys.argv[3]
    port = 7

    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((ip, port))

    t_send = threading.Thread(target=sender, args=(sock, input_path))
    t_recv = threading.Thread(target=receiver, args=(sock, output_path))

    t_recv.start()
    t_send.start()

    t_send.join()
    t_recv.join()

    sock.close()


if __name__ == "__main__":
    main()