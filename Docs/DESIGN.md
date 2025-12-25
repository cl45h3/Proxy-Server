# Custom Network Proxy Server – Design Document

## 1. High-Level Architecture

The proxy server follows a **multi-threaded, concurrent connection model**. It acts as an intermediary between a client (e.g., `curl`, web browser) and an upstream server (e.g., Google, example.com).

### Component Diagram

```
[ Client ]
     |
     v
[ Proxy Listener (Port 8888) ]
     |
     v
[ Dispatcher Thread ]
     |
     +-----------------------------+
     |                             |
     v                             v
[ Worker Thread A ]         [ Worker Thread B ] ...
     |                             |
     v                             v
[ Parser ] -> [ Filter ]     [ Parser ] -> [ Filter ]
     |                             |
     v                             v
[ Forwarder / Tunneling ]   [ Forwarder / Tunneling ]
     |                             |
     v                             v
[ HTTP Server ]           [ HTTPS Server ]
```

### Core Components

- **Listener (`main.c`)**  
  A TCP socket bound to port `8888` that listens for incoming client connections.

- **Dispatcher (`main.c`)**  
  The main accept loop that accepts client connections and spawns a new **worker thread** for each client using the Windows `CreateThread` API.

- **Request Parser (`parser.c`)**  
  Parses raw HTTP headers to extract the request `Method` (GET, POST, CONNECT), destination `Host`, and `Port`.

- **Filter Engine (`proxy.c`)**  
  Checks the extracted host against a blacklist configuration file (`config/blocked.txt`).

- **Tunneling Module (`proxy.c`)**  
  Handles HTTPS `CONNECT` requests by creating a blind, bidirectional data tunnel using `select()`.

- **Forwarder (`proxy.c`)**  
  Handles standard HTTP requests by resolving DNS, connecting to the upstream server, and forwarding request/response data.

- **Logger (`logger.c`)**  
  A thread-safe logging mechanism using a global mutex to serialize writes to `src/logs/proxy.log`.

---

## 2. Concurrency Model

### Chosen Model: Thread-per-Connection

**Rationale**

- Network I/O is inherently blocking and latency-prone.
- Handling requests sequentially would cause a single slow request to block all other clients.
- A thread-per-connection model allows each client to be serviced independently.

**Implementation**

- The main thread performs only lightweight operations (socket accept).
- Each accepted connection is handled by a dedicated worker thread.
- DNS resolution, upstream connections, and data forwarding occur inside worker threads.

**Synchronization**

- Shared resources (primarily the log file) are protected using a global mutex to prevent race conditions and corrupted log output.

---

## 3. Data Flow

### Scenario A: Standard HTTP Request (e.g., `http://example.com`)

1. **Read**  
   The proxy reads the full HTTP request header from the client socket.

2. **Parse**  
   The request parser extracts `Host: example.com` and `Port: 80`.

3. **Filter**  
   The host is checked against `blocked.txt`.  
   If blocked, the proxy immediately responds with `HTTP/1.1 403 Forbidden`.

4. **Connect**  
   The proxy resolves DNS (IPv4 preferred) and opens a TCP connection to the destination server.

5. **Forward**  
   The original client request buffer is forwarded to the upstream server.

6. **Relay**  
   The proxy enters a loop, reading data from the server and writing it back to the client until the connection closes.

---

### Scenario B: HTTPS Tunneling (e.g., `https://google.com`)

1. **Read**  
   The proxy receives:  
   `CONNECT google.com:443 HTTP/1.1`

2. **Connect**  
   A TCP connection is established to `google.com` on port `443`.

3. **Acknowledge**  
   The proxy sends:  
   `HTTP/1.1 200 Connection Established`  
   back to the client.

4. **Tunnel**  
   The proxy enters a `select()` loop monitoring both sockets:
   - Data from **Client → Server** is forwarded unchanged.
   - Data from **Server → Client** is forwarded unchanged.

   The proxy does **not** inspect or modify encrypted TLS traffic and functions purely as a blind transport pipe.

---

## 4. Error Handling & Limitations

- **IPv6 Handling**  
  The proxy forces `AF_INET` (IPv4) resolution to avoid instability on Windows systems with partial or misconfigured IPv6 support.

- **Connection Persistence**  
  HTTPS tunnels support persistent connections, while standard HTTP connections are typically closed after a single request/response cycle to simplify resource management.

- **Security Considerations**  
  The proxy provides basic domain filtering only.  
  It does **not** perform SSL inspection or man-in-the-middle interception, preserving end-to-end encryption for HTTPS traffic.