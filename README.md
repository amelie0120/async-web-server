# Asynchronous Web Server

## Overview

I implemented a high-performance asynchronous web server that leverages several advanced I/O operations in Linux. This project helped me:

- Deepen my understanding of socket programming
- Implement and design applications using asynchronous I/O operations
- Master advanced I/O techniques in the Linux operating system

The resulting web server efficiently handles multiple concurrent connections using a non-blocking, event-driven design pattern.

## Features Implemented

### Advanced I/O Operations

I successfully implemented a web server that utilizes the following advanced I/O techniques:

- **Asynchronous File Operations**: Used Linux's asynchronous I/O API to read files without blocking
- **Non-blocking Socket Operations**: Implemented non-blocking I/O for socket connections to handle multiple clients simultaneously
- **Zero-copying**: Utilized the `sendfile()` system call to transfer file data directly from kernel space to the socket without copying to user space
- **I/O Multiplexing**: Employed the `epoll` API to efficiently monitor multiple file descriptors for events

### HTTP Protocol Implementation

The server implements a focused subset of the HTTP protocol for file serving:

- Properly formatted HTTP responses with appropriate headers
- HTTP 200 responses for successful file retrieval
- HTTP 404 responses for invalid paths
- Connection closing after file transmission in accordance with the HTTP protocol

### Resource Handling

I implemented two distinct file-serving methods based on the requested resource type:

- **Static Files**: Files from the `static/` directory are transmitted using zero-copy operations (`sendfile()`) for maximum performance
- **Dynamic Files**: Files from the `dynamic/` directory are processed asynchronously and served using non-blocking socket operations

## Implementation Details

### State Machine Architecture

I designed a state machine for each connection to track its progress through the various stages:

- Connection establishment
- Request parsing
- Resource identification
- Response preparation
- File transmission
- Connection termination

This approach allowed for clean handling of the asynchronous nature of the operations.

### Connection Management

Each connection is tracked using a dedicated data structure containing:

- Socket file descriptor
- Current state in the state machine
- Buffer for request/response data
- File information and transmission progress
- Asynchronous operation context

### HTTP Request Parsing

I integrated an HTTP parser to extract the requested resource path from client requests, implementing the callback mechanism to handle the parsed data correctly.

### Event-driven Architecture

The server uses an event-driven architecture to handle I/O events:

1. Waits for events on monitored file descriptors using `epoll`
2. Processes events as they occur (new connections, data available, write possible, etc.)
3. Updates the state machine for each connection
4. Initiates appropriate asynchronous operations
5. Monitors for completion of those operations

## Technical Highlights

- **Efficient Resource Usage**: The server optimizes system resource usage by avoiding thread per connection models
- **Scalability**: Can handle hundreds of concurrent connections with minimal resource overhead
- **High Performance**: Zero-copy and asynchronous I/O significantly improve throughput
- **Reliable Error Handling**: Implemented robust error handling throughout the codebase

## Resources

During implementation, I utilized the following references:

- [sendfile manual](https://man7.org/linux/man-pages/man2/sendfile.2.html)
- [Linux AIO documentation](https://man7.org/linux/man-pages/man2/io_setup.2.html)
- [epoll documentation](https://man7.org/linux/man-pages/man7/epoll.7.html)
