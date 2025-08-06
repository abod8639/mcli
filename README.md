# MCLI: Minimal Command-Line Interface for Embedded Systems

A lightweight CLI library for constrained embedded systems 
- **Non-blocking**: Suitable for single-threaded apps without blocking the main loop
- **No dynamic memory**: Uses static buffers only
- **Hardware-agnostic**: Abstract IO interface supports various communication methods

Packaged in are IO adapters for...
- Arduino Serial Library
- ESP32 UART
- ESP32 STA Wifi
- Microblaze V AXI Uartlite (soon)

## Examples:

Several implemented examples can be found in the [mcli-examples](https://github.com/ryanfkeller/mcli-examples) repo. 

The GIF below shows the ESP32 WiFi CLI demo in action, featuring:
* Telnet compatibility via PuTTy
* Built-in `help` command
* Custom command definitions
* Connection recovery after disconnect

![ESP32 WiFi MCLI Example](https://github.com/ryanfkeller/mcli-examples/blob/main/assets/esp32_wifi_sta_mcli.gif "ESP32 WiFi MCLI Example")

## 🙋 Help Wanted!

I could use your feedback on how I can make this tool more useful for you. My focus areas for improvements:
- Portability across different platforms
- Memory optimization opportunities  
- API design clarity and consistency
- Missing features you'd like to see

Feel free to open issues or PRs with suggestions!

---

## Implementing this CLI in your Project

### 1. Make or include an I/O Adapter

The I/O adapter connects the CLI engine to your device’s communication interface 

Adapters extend `CliIoInterface` to add platform-specific communication, while `CliIoInterface` provides higher level functionality (`print`, `println`, `printf`, etc.).

**Using an existing adapter:**
```cpp
#include "mcli_arduino_serial.h"
ArduinoSerialIo io(Serial);
```

**Creating a custom adapter:**
```cpp
class MyIOAdapter : public mcli::CliIoInterface {
    // Implement put_byte(), get_byte(), byte_available()
};
```

---

### 2. Define your application context

The application context is a user-defined struct that holds references to all hardware interfaces, drivers, or other dependencies your CLI commands may need. This enables clean dependency injection, keeping commands modular and testable.

```cpp
// app_context.h
struct MyAppContext {
    MyIOAdapter& io; // Optional: include your IO adapter for in-function printing. 
    Gpio& gpio;
    Sensor& temp_sensor;
    // Add other peripherals or variables here
};
```

---

### 3. Create commands

The CLI operates on command functions that have the following signature: 
```cpp
void my_command(const mcli::CommandArgs args, MyAppContext* ctx);
```

**CommandArgs structure**
```cpp
struct CommandArgs {
    int argc;                            // Number of arguments
    char argv[MAX_ARGS][MAX_ARG_LENGTH]; // Argument strings
};
```

**Example commands:**
```cpp
#include "mcli.h"

void toggle_led(const mcli::CommandArgs args, MyAppContext* ctx) {
    ctx->gpio.set(LED_ID, "ON");
}

void read_temperature(const mcli::CommandArgs args, MyAppContext* ctx) {
   uint32_t temp_celsius = ctx->temp_sensor.read_celsius();
   ctx->io.printf("Temperature: %lu°C\n", temp_celsius);
}

void print_args(const mcli::CommandArgs args, MyAppContext *ctx) {
   ctx->io.println("Command Test Demo\n");
   ctx->io.printf("argc: %d\n", args.argc);
   ctx->io.println("argv: ");
   for(int i=0; i<args.argc; i++)
   {
      ctx->io.printf("%s ",args.argv[i]);
   }
}
```

**Define your command table**
```cpp
const mcli::CommandDefinition<MyAppContext> commands[] = {
    {"led",  toggle_led,       "Toggle LED state"},
    {"temp", read_temperature, "Read temperature sensor"},
    {"args", print_args,       "Print command arguments"}
};
```

---

### 4. Bring it all together

Arduino simple example
```cpp
#include "mcli.h"
#include "mcli_arduino_serial.h"

// Initialize components
ArduinoSerialIo io(Serial);
Gpio gpio;
Sensor temp_sensor; 
MyAppContext ctx{io, gpio};

// Create CLI engine with components and command table
mcli::CliEngine<MyAppContext> cli(io, ctx, commands);

void setup() {
    Serial.begin(9600);
    gpio.init();
    temp_sensor.init();
}

void loop() {
    cli.process_input();  // Call this regularly in your main loop
}
```
That's it! Your CLI is ready with an automatic `help` command alongside all your custom commands.

---

## Directory Structure

```text
include/mcli/
└──mcli.h                   # Unified CLI types, engine, interface

include/adapters/
├── mcli_arduino_serial.h    # Arduino Stream-based adapter
├── mcli_esp32_uart.h        # ESP32 UART adapter (uses FreeRTOS driver)
└── mcli_esp32_wifi_sta.h    # ESP32 WiFi STA adapter (uses FreeRTOS driver)
```

## Built-in Commands

- `help` — Lists all available commands with descriptions

---

## Integration Options

### Copy Files
- Copy `mcli.h` and desired adapters to your project
- Add to include path
- Include in your code

### Git Submodule

```bash
git submodule add https://github.com/ryanfkeller/mcli.git libs/mcli
```

---

**Scaling Estimates:**
- **Arduino Uno/Nano**: Likely comfortable for 5-15 commands with basic functionality
- **Arduino Mega**: Can handle 20+ complex commands with plenty of headroom
- **ESP32/similar**: Memory usage becomes negligible compared to available resources

---

## Configuration Constants

Defined in `mcli.h`:

| Constant          | Description            | Default  |
| ----------------- | ---------------------- | -------- |
| `MAX_ARGS`        | Max number of CLI args | 5        |
| `MAX_ARG_LENGTH`  | Max chars per argument | 16       |
| `CMD_BUFFER_SIZE` | Input buffer size      | 128      |
| `DEFAULT_PROMPT`  | CLI prompt string      | `mcli> ` |


---

## License

MIT License — see LICENSE file for details

---
