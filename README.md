# Asynchronous Web Server (AWS)

## Overview

This Asynchronous Web Server (AWS) project is designed to deepen understanding and skills in working with sockets, asynchronous operations, and advanced I/O operations within the Linux operating system. Utilizing advanced I/O operations such as asynchronous file operations, non-blocking socket operations, zero-copying, and I/O multiplexing, this server offers a limited implementation of the HTTP protocol focused on file delivery to clients.

## Features

- **Multiplexing I/O Operations:** Uses epoll for efficient I/O operations over network sockets.
- **Asynchronous File Operations:** Employs asynchronous API for reading dynamic files, optimizing performance.
- **Non-blocking Socket Operations:** Ensures that socket operations do not block the server's execution, allowing for handling multiple connections simultaneously.
- **Zero-copy File Transfer:** Utilizes the zero-copying API (sendfile) for efficient static file transmission.
- **HTTP Protocol Support:** Handles basic HTTP requests for serving static and dynamically processed files with proper HTTP response codes.

## Prerequisites

- Linux operating system with support for epoll, sendfile, and asynchronous I/O operations.
- C compiler (GCC recommended) for building the server.
- Basic knowledge of networking and HTTP protocol.

## Installation

1. Clone the repository to your local machine.
2. Navigate to the project directory.
3. Compile the server using the provided Makefile: `make`

## Configuration

Before running the server, configure the following environment variables to specify the document root:

- `AWS_DOCUMENT_ROOT`: The root directory from which files will be served. It should contain `static` and `dynamic` subdirectories for corresponding file types.

## Running the Server

To start the server, execute the compiled binary with the command: ./aws

The server listens on port defined by `AWS_LISTEN_PORT` (default: 8888).

## Testing

Automated tests are provided in the `tests` directory. To run the tests:

1. Ensure the server is compiled and the `aws` executable is present.
2. Navigate to the `tests` directory.
3. Run `./run.sh` to execute all tests.

## Project Structure

- `aws.c`: Main server implementation file.
- `aws.h`: Header file with necessary definitions and structure declarations.
- `tests/`: Directory containing automated tests for the server.
- `http-parser/`: HTTP parser utilized for request handling.

## Contributing

Contributions to improve the server or extend its capabilities are welcome. Please submit pull requests with detailed descriptions of changes or improvements.

## License

This project is licensed under the MIT License - see the LICENSE file for details.