#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
 
namespace mcli {

    // =============================================================================
    // CLI TYPES
    // =============================================================================

    // Configuration constants
    constexpr int MAX_ARGS = 5;
    constexpr int MAX_ARG_LENGTH = 12;
    constexpr size_t CMD_BUFFER_SIZE = 128;
    constexpr const char* DEFAULT_PROMPT = "\x1b[1mmcli> \x1b[0m";

    // Calculate total CommandArgs memory usage at compile time
    constexpr size_t COMMAND_ARGS_SIZE = sizeof(int) + (MAX_ARGS * MAX_ARG_LENGTH);
    static_assert(COMMAND_ARGS_SIZE <= 300, "CommandArgs too large for constrained systems");

    // Command argument structure
    struct CommandArgs {
        int argc;
        char argv[MAX_ARGS][MAX_ARG_LENGTH];

        CommandArgs() : argc(0) {
            memset(argv, 0, sizeof(argv));
        }
    };

    // Generic command function signature
    template<typename ContextType>
    using CommandFunction = void(*)(const CommandArgs args, ContextType* ctx);

    // Command definition structure
    template<typename ContextType>
    struct CommandDefinition {
        const char* name;
        CommandFunction<ContextType> execute;
        const char* help;
    };

    // =============================================================================
    // CLI I/O INTERFACE
    // =============================================================================

    /**
     * Abstract interface for CLI I/O operations
     * Platform-specific implementations should inherit from this interface
     */
    class CliIoInterface {
    public:
        virtual ~CliIoInterface() = default;

        /**
         * Core interface -- derived classes must implement these
         */
        virtual void put_byte(char c) = 0;
        virtual char get_byte() = 0;
        virtual bool byte_available() = 0;

        /**
         * Bulk interface -- recommend override for packet-based interfaces
         */
        virtual void put_bytes(const char* data, size_t len) {
            for (size_t count = 0; count < len; count++) {
                put_byte(data[count]);
            }
        }

        virtual size_t get_bytes(char* buffer, size_t max_len) {
            size_t count = 0;
            while (count < max_len && byte_available()) {
                buffer[count++] = get_byte();
            }
            return count;
        }

        /**
         * High level functions -- default behavior can be overridden if needed
         */
        virtual void print(const char* str) {
            if (!str) return;

            size_t len = strlen(str);
            if (len > 0) {
                put_bytes(str, len);
            }
        }
        virtual void println() {
            print("\r\n");
        }
        virtual void println(const char* str) {
            print(str);
            println();
        }
       virtual void printf(const char* fmt, ...) {
            if (!fmt) return;
    
            va_list args;
            va_start(args, fmt);
    
            // Small stack-based buffer for embedded systems
            char buffer[64];
            int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
            va_end(args);
    
            if (len > 0) {
                // Ensure null termination and handle truncation
                if (static_cast<uint32_t>(len) >= sizeof(buffer)) {
                    len = sizeof(buffer) - 1;
                    buffer[len] = '\0';
                }
                put_bytes(buffer, len);
            }
        }

        virtual void flush() {
            // Default implementation is no-op since byte-level interface
            // assumes immediate transmission. Override if buffering is used.
        }
        
        // Optional terminal control methods
        // (Can be overridden for enhanced/alternate functionality)
        virtual void clear_screen() {
            print("\x1b[2J\r\n");
        }

        virtual void send_prompt(const char* prompt = DEFAULT_PROMPT) {
            print(prompt);
        }

        virtual void send_backspace() {
            print("\b \b");
        }
    };

    // =============================================================================
    // CLI ENGINE
    // =============================================================================

    /**
     * Generic CLI engine that handles command parsing, dispatch, and I/O. 
     * Template parameter allows for any application-specific context type.
     */
    template<typename ContextType>
    class CliEngine {
        public:
            /**
             * Constructor
             * @param io Reference to I/O interface implementation
             * @param context Reference to application-specific context
             * @param prompt Custom prompt string (optional)
             */
            template<size_t N>
            CliEngine(
                CliIoInterface& io,
                ContextType& context,
                const CommandDefinition<ContextType> (&commands)[N],
                const char* prompt = DEFAULT_PROMPT )
                : io_(io), context_(context), 
                commands_(commands), command_count_(N),
                prompt_(prompt) {}


            /**
             * Main CLI loop -- runs indefinitely processing commands
             */
            void process_input() {
                if (!prompt_sent_) {
                    io_.send_prompt(prompt_);
                    prompt_sent_ = true;
                }

                CommandArgs args = get_command_input();
                if (args.argc > 0) {
                    if (!dispatch_command(args)) {
                            io_.print("Command \"");
                            io_.print(args.argv[0]);
                            io_.println("\" not found. Type 'help' for available commands.");
                    }
                    prompt_sent_ = false;
                }
            }

            /**
             * Process a single command line (useful for testing or non-interactive use)
             * @param command_line String containing the command and arguments
             * @return true if command was found and executed, false otherwise
             */
            bool execute_command(const char* command_line) {
                CommandArgs args = parse_command_line(command_line);
                return dispatch_command(args);
            }

            /**
             * Print available commands
             */
            void print_help() {
                // Figure out the longest command name for padding
                size_t max_name_len = strlen("help");
                for (size_t i = 0; i < command_count_; i++) {
                    size_t len = strlen(commands_[i].name);
                    if (len > max_name_len) {
                        max_name_len = len;
                    }
                }

                // Build padding formatter
                char fmt[32];
                snprintf(fmt, sizeof(fmt), "  %%-%ds -- %%s\r\n", max_name_len);

                io_.println();
                io_.println("Available commands:");

                // Show built-in commands
                io_.printf(fmt, "help", "Show available commands");

                // Show user commands
                if (command_count_ > 0) {
                    for (size_t i = 0; i < command_count_; i++) {
                        io_.printf(fmt, commands_[i].name, commands_[i].help);
                    }
                } else {
                    io_.println("  (No additional commands registered)");
                }
                io_.println();
            }

            // Reset CLI state between connections
            void reset_session() {
                // Clear input buffer
                memset(input_buffer_, 0, sizeof(input_buffer_));
                input_pos_ = 0;
                last_line_char_ = 0;
                prompt_sent_ = false;
            }
            
        private:
            // Parse command line into argc/argv format
            CommandArgs parse_command_line(const char* input) {
                CommandArgs args;

                // Work with a local copy for tokenization
                char temp_buffer[CMD_BUFFER_SIZE];
                strncpy(temp_buffer, input, CMD_BUFFER_SIZE-1);
                temp_buffer[CMD_BUFFER_SIZE - 1] = '\0';

                char* token = temp_buffer;
                while (*token && args.argc < MAX_ARGS - 1) {
                    // Skip leading spaces
                    while (*token == ' ') token++;
                    if (!*token) break;
                
                    // Token start
                    // Copy token directly into args.argv[args.argc]
                    char* dest = args.argv[args.argc];
                    int char_count = 0;
                    while (*token && *token != ' ' && char_count < MAX_ARG_LENGTH - 1) {
                        *dest++ = *token++;
                        char_count++;
                    }
                    *dest = '\0';
                
                    args.argc++;
                
                    // Skip trailing spaces
                    while (*token == ' ') token++;
                }
            
                return args;
            }

            // Get command input from user with echo and backspace support
            CommandArgs get_command_input() {
                // Read processing buffer
                char read_buffer[32];
                size_t buffer_len = 0;

                // Try to read available data (non-blocking)
                buffer_len = io_.get_bytes(read_buffer, sizeof(read_buffer));
                if (buffer_len == 0) {
                    // No data available, return empty command
                    return CommandArgs();
                }

                // Process each character from the buffer
                for (size_t i = 0; i < buffer_len; i++) {
                    uint8_t in_char = 0;

                    // Get the next character from the buffer
                    in_char = read_buffer[i];
                
                    // Handle backspace
                    if (in_char == 8 || in_char == 127) {
                        if (input_pos_ > 0) {
                            input_pos_--;
                            input_buffer_[input_pos_] = '\0';
                            io_.send_backspace();
                        }
                        continue;
                    }
                
                    // Handle CRLF
                    if (in_char == '\r' || in_char == '\n') {
                        // If this is \n and we just processed \r, skip it
                        if ((in_char == '\n') && (last_line_char_ == '\r')) {
                            last_line_char_ = in_char;
                            continue;
                        }
                        last_line_char_ = in_char;

                        io_.println();

                        // Only break if we've typed something
                        if (input_pos_ > 0) {
                            input_buffer_[input_pos_] = '\0';
                            CommandArgs result = parse_command_line(input_buffer_);

                            // Reset input buffer
                            memset(input_buffer_, 0, sizeof(input_buffer_));
                            input_pos_ = 0;

                            return result;
                        } 

                        prompt_sent_ = false;
                        
                        continue;
                    }
                
                    // Handle regular characters
                    if (input_pos_ < CMD_BUFFER_SIZE - 1) {
                        io_.put_byte(in_char);
                        input_buffer_[input_pos_] = in_char;
                        input_pos_++;
                    }
                }
            
                // No complete command yet
                return CommandArgs();
            }

            // Find and execute a command
            bool dispatch_command(const CommandArgs& args) {
                if (args.argc == 0) {
                    return false;
                }
            
                // Handle built-in commands first
                if (strcmp(args.argv[0], "help") == 0) {
                    print_help();
                    return true;
                }
            
                // Handle user-registered commands
                if (!command_count_) {
                    return false;
                }
            
                for (size_t i = 0; i < command_count_; i++) {
                    if (strcmp(args.argv[0], commands_[i].name) == 0) {
                        commands_[i].execute(args, &context_);
                        return true;
                    }
                }
            
                return false;
            }

            // Member variables
            CliIoInterface& io_;
            ContextType& context_;
            const CommandDefinition<ContextType>* commands_;
            size_t command_count_;
            const char* prompt_;

            // Input parsing
            char input_buffer_[CMD_BUFFER_SIZE];
            size_t input_pos_ = 0;
            uint8_t last_line_char_ = 0;
            bool prompt_sent_ = false;
    };
}