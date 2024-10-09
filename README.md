# Multithreaded Email Server

This repository contains a multithreaded implementation of an **SMTP** and **POP3** email server, capable of handling concurrent client connections. It supports standard email operations as defined by the RFC protocols, including commands such as `HELO`, `MAIL FROM`, `RCPT TO`, `DATA`, and `QUIT`.

## Features
- **Multithreaded Design**: Supports multiple client connections simultaneously.
- **SMTP Protocol Support**: Implements basic SMTP commands like `HELO`, `MAIL FROM`, `RCPT TO`, `DATA`, and `QUIT`.
- **POP3 Protocol Support**: Handles commands for retrieving and managing emails.
- **Command Handling**: Manages email data and handles errors gracefully.
- **Buffer Management**: Efficient data transfer between client and server.
- **Tested with Telnet and Automated Scripts**: Ensures reliable performance and correctness of the email server through extensive testing.

## Technologies Used
- **C++**: The core server implementation.
- **Multithreading**: Achieves concurrency using C++ threads.
- **Socket Programming**: Used for TCP connections to interact with clients.
- **Telnet**: For testing the email server functionality.

## Installation & Usage

### Prerequisites
- C++ compiler (e.g., g++)
- Telnet and Thunderbird client (for testing)
  
### Installation

1. Clone the repository:
    ```bash
    git clone https://github.com/mikth3372/Email-Server.git
    cd Email-Server
    ```

2. Compile the code:
    ```bash
    make
    ```

3. Run the smtp server:
    ```bash
    ./smtp -p <port-number> <mailbox-directory>
    ```

4. Run the pop3 server:
   ```bash
    ./pop3 -p <port-number> <mailbox-directory>
    ```

### Usage

Once the server is running, you can connect to it using a **Telnet** client or an automated script:

1. Open a terminal and use Telnet to connect:
    ```bash
    telnet localhost 10000
    ```

2. Test various commands, such as:
    ```
    HELO example.com
    MAIL FROM:<sender@example.com>
    RCPT TO:<recipient@example.com>
    DATA
    <email body>
    .
    QUIT
    ```

### Testing
- The server has been tested using **Telnet** and various automated scripts to ensure correct command handling and concurrent client connections.
- It can support up to 100 concurrent connections without issues.

## Future Enhancements
- **TLS/SSL Support**: Adding encryption for secure email transmission.
- **Email Storage**: Integrating a database or file system for storing and retrieving emails.
- **Error Handling Improvements**: Enhanced error messages and logging.

---

### Connect with Me
- **GitHub**: [mikth3372](https://github.com/mikth3372)
- **LinkedIn**: [linkedin.com/in/mikhaelthomas](https://www.linkedin.com/in/mikhaelthomas)
