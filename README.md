# MCLI: Minimal Command-Line Interface for Embedded Systems

A lightweight, embedded-friendly command-line interface (CLI) library designed for bare-metal or constrained systems.  

## Features

- **Hardware Agnostic**: Works with any transport (UART, USB, etc.) via pluggable I/O interface
- **No Dynamic Memory**: Statically defined commands and fixed-size buffers
- **Application Context Support**: Fully type-safe template-based context injection
- **Extensible**: Drop-in commands and portable I/O adapters
- **Arduino-Friendly**: Includes ready-made adapter for Arduino `Stream`-based devices

---

## Quick Start

### 1. Define Your Application Context

The application context is a user-defined struct that holds references to all hardware interfaces, drivers, or other dependencies your CLI commands need. Passing this context into command functions allows for clean dependency injection, keeping your commands modular and testable.

```cpp
// app_context.h
struct MyAppContext {
    Stream& serial;
    // Add other peripherals here
};
```

---

### 2. Implement I/O Adapter

The I/O adapter connects the CLI engine to your device’s communication interface, and handles sending and receiving characters in a way that matches your hardware or transport (like UART, USB, etc.). 

Adapters extend the base `CliIoInterface` to add low-level communication info, while `CliIoInterface` provides higher level functionality (`print`, `println`, `printf`, etc.).

The following implemented adapters are already provided:

- **ArduinoSerialIo** (`platform_adapters/include/mcli/arduino_serial_io.h`) — works with any Arduino `Stream` such as `Serial` or `Serial1`


You can alternatively create a custom I/O adapter for other platforms by extending `CliIoInterface` and implementing the required methods:

```cpp
class MyAdapter : public mcli::CliIoInterface {
    // Implement put_byte(), get_byte(), byte_available()
};
```

---

### 3. Create Commands

```cpp
// app_commands.cpp
#include "mcli.h"

void hello_cmd(const mcli::CommandArgs args, MyAppContext* ctx) {
    ctx->serial.println("Hello from CLI!");
}

const mcli::CommandDefinition<MyAppContext> commands[] = {
    mcli::make_command("hello", hello_cmd, "Say hello"),
};

constexpr size_t command_count = sizeof(commands) / sizeof(commands[0]);
```

---

### 4. Initialize and Run

```cpp
#include "mcli.h"
#include "mcli/arduino_serial_io.h"

MyAppContext ctx{Serial};
ArduinoSerialIo io(Serial);

mcli::CliEngine<MyAppContext> cli(io, ctx);
cli.register_commands(commands, command_count);

void loop() {
    cli.process_input();  // Call this regularly in your main loop
}
```

---

## Directory Structure

```text
core/include/mcli/
├── mcli.h                   # Unified CLI types, engine, interface

platform_adapters/include/mcli/
└── arduino_serial_io.h      # Arduino Stream-based adapter
```

---

## API Overview

### `mcli::CliEngine<ContextType>`

- `CliEngine(CliIoInterface& io, ContextType& ctx, const char* prompt = "mcli> ")`
- `void register_commands(...)`
- `void process_input()` — Handles input from users
- `bool execute_command(const char* line)` — Run commands programmatically
- `void print_help()` — Built-in `help` command prints all registered commands

---

### `mcli::CliIoInterface`

To support any custom I/O transport, implement:

- `put_byte(char)`
- `get_byte()`
- `byte_available()`

You can also override:
- `put_bytes(...)`, `get_bytes(...)`, `print(...)`, `printf(...)`, `send_prompt(...)`, `flush()`, `send_backspace()`, `clear_screen()`

---

### Command Definition

```cpp
const mcli::CommandDefinition<MyContext> cmds[] = {
    mcli::make_command("cmd", my_func, "Help text")
};
```

Each command must follow the signature:

```cpp
void my_func(const mcli::CommandArgs args, MyContext* ctx);
```

---

## Configuration Constants

Defined in `mcli.h`:

- `MAX_ARGS` – Max number of args (default: 5)
- `MAX_ARG_LENGTH` – Max chars per arg (default: 16)
- `CMD_BUFFER_SIZE` – Input buffer size (default: 128)
- `DEFAULT_PROMPT` – Default prompt string (`"mcli> "`)

---

## Example Use Case

- Works with Arduino or bare-metal platforms
- UART/USB interface via `Stream`
- Add LED toggle commands, sensor readouts, debug interfaces, and more

---

## Integration Options

### Copy Files

- Copy files from `core/include/mcli/` and any `platform_adapters/include/mcli/` you need
- Add them to your project include path

### Git Submodule

```bash
git submodule add <repo-url> libs/mcli
```

Include with:

```cpp
#include "mcli/mcli.h"
#include "mcli/arduino_serial_io.h"
```

---

## License

MIT or similar — [insert license details here]

---
