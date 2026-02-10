# IRC Server

Multi-threaded IRC server written in **C** using POSIX sockets and pthreads.

Backend component for the [KMP IRC Client](https://github.com/ingridoo123/Kotlin-MultiPlatfrom-Aplikacja-Sieciowa).

---

## Tech Stack

| Component | Technology |
|---|---|
| Language | C |
| Networking | POSIX Sockets (TCP) |
| Concurrency | pthreads (thread-per-client) |
| Protocol | Custom IRC-like text protocol |

---

## Supported Commands

| Command | Description |
|---|---|
| `NICK <nickname>` | Set your nickname |
| `JOIN <#channel>` | Join or create a channel |
| `LEAVE <#channel>` | Leave a channel |
| `MSG <#channel> <text>` | Send a message to a channel |
| `PRIVMSG <nick> <text>` | Send a private message |
| `LIST` | List all channels |
| `USERS <#channel>` | List users in a channel |
| `HELP` | Show available commands |
| `QUIT` | Disconnect |

---

## How to Run

**Build and start:**
```bash
gcc -Wall irc_server.c -o irc_server
./irc_server
```

Server listens on port **8080** by default. A `#general` channel is created automatically on startup.

**Test with netcat:**
```bash
nc localhost 8080
```

---

## Configuration

Constants defined in `server.c`:

| Constant | Default | Description |
|---|---|---|
| `PORT` | 8080 | Server listening port |
| `MAX_CLIENTS` | 100 | Max concurrent connections |
| `MAX_CHANNELS` | 50 | Max number of channels |
| `BUFFER_SIZE` | 2048 | Read buffer size per client |

---

## License

This project was created for educational purposes.
