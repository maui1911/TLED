# TLED Project - Claude Code Instructions

## Project Overview

TLED is a Matter-over-Thread LED controller firmware for the DFRobot Beetle ESP32-C6. See `docs/master-plan.md` for full project specification.

## Development Approach

**Test-Driven Development (TDD)** is required for this project:

1. **Write tests first** - Before implementing any feature, write failing tests that define the expected behavior
2. **Red-Green-Refactor** - Follow the TDD cycle:
   - Red: Write a failing test
   - Green: Write minimal code to make the test pass
   - Refactor: Clean up the code while keeping tests green
3. **Test coverage** - All driver functions and Matter callbacks must have corresponding unit tests
4. **Integration tests** - Test the interaction between Matter stack and hardware drivers

## Testing Framework

- Use ESP-IDF's Unity test framework for unit tests
- Tests live in `test/` directory
- Run tests with `idf.py build` and flash to device, or use QEMU where possible

## Code Style

- C++ for application code (`.cpp`)
- Follow ESP-IDF coding conventions
- Use `static const char *TAG` for logging tags
- Prefer explicit error handling with `esp_err_t`
- No magic numbers - use `#define` or `constexpr` in `app_config.h`

## Build Commands

```bash
# Set up environment
get_matter
set_cache

# Build
idf.py build

# Flash and monitor
idf.py -p /dev/ttyACM0 flash monitor

# Run tests
idf.py -p /dev/ttyACM0 flash monitor -T test
```

## Hardware

- **Board:** DFRobot Beetle ESP32-C6 v1.1
- **Onboard LED:** GPIO15 (used in Phase 1)
- **LED Strip Data:** GPIO4 (used in Phase 2+)
- **Boot Button:** GPIO9

## Project Phases

- **Phase 1:** On/Off Light over Matter + Thread (current)
- **Phase 2:** RGB Light with LED Strip
- **Phase 3:** Smooth Transitions & Effects
- **Phase 4:** Runtime Configuration
- **Phase 5:** Production Polish
