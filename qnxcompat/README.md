# QNX-compatible messaging in user-space

This is an experimental user-space implementation of the QNX local
messaging APIs. Please study & discuss.

Supports:

  * Send/Receive/Reply (SRR) API functions (QNX4 IPC)
  * Unix sockets as the backend
  * Peer exit detection, both server and client
  * Message caching (for repeated Receive and delayed Reply)
  * PID based peer identification

Limitations:

  * Not the complete API. I don't even have the full description.
  * Unoptimized socket handling
  * Unoptimized data structures
  * Messages from clients are limited to (arbitrary) 65536 bytes.
  * No QNX6 IPC support (QNX4 was more challenging)

## QNX4 synchronous messaging

### API calls

`Send()`:

  * Blocks in `send()` and `recv()` system calls until the outgoing call is
    fully processed and replied by the server.

`Receive()`:

  * Blocks in `accept()` and `recv()` until an incoming call is retrieved
    from the socket.
  * When a certain source process ID is requested, caches all other
    incoming calls until it receives a call from the specified source.
  * Before attempting to retrieve new messages from the socket, cache is
    examined for unprocessed incoming calls.
  * Can be called repeatedly on the same incoming call (specified by
    process ID) to retrieve more data.

`Reply()`:

  * Sends a reply to the client socket.
  * Cleans up the data structures.

### Ideas

  * We could receive data with `MSG_PEEK` instead of storing
    them in a buffer.
      - That would allow for large message detection and handling.
      - Message size would still be limited by the kernel socket buffer.
      - Server would always have to call an additional (possibly
        zero-length) `recv()` in `Reply()` to consume the message.
  * We could pass shared memory (using memfd) in the message rather than
    the data.
      - We could to that conditionally when a certain threshold was
        exceeded.
  * We could use automatic persistent connections for client/server pairs.
      - First message between a specific client/server pair would create a
        long-term connection socket.
      - All messages between the two endpoints would be set as packets over
        that link.
      - We could then support priority inversion via a mutex locked by the
        server at all times except for a short period of time after reply.
      - Such a mutex would be shared at the connection setup time via
        shared memory by passing a memfd.
  * We might want to look at thread and fork safety.
      - Not supported under QNX4 according to my information.
      - It might be an idea to identify individual threads rather than
        proceses or to make the server calls locked properly.
  * We could look at what netlink offers.


## References

  * https://www.qnx.com/developers/docs/qnx_4.25_docs/qnx4/sysarch/microkernel.html
