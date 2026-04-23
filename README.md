# app_rpt

AllStarLink app_rpt is the core radio repeater controller module for Asterisk, providing functionality for amateur radio repeaters, links, and nodes. This repository contains the radio interface modules, signaling protocols, and repeater logic used in AllStarLink 3.

## What This Does
This code implements:
- Radio interface drivers (USB, serial, USRP, voter)
- Repeater control logic (courtesy tones, timeout, hang time)
- Digital signaling protocols (MDC1200, POCSAG)
- Radio bridging and linking functionality
- Configuration management and CLI commands
- Telemetry and monitoring systems

## Repository Structure
```text
app_rpt/
├── apps/              # Core repeater application logic (app_rpt.c)
    ├──app_rpt/        # Core repeater application logic support files
├── channels/          # Asterisk channel drivers (radio interfaces)
├── configs/           # Configuration files and examples
    ├── rpt/           # Configuration files and examples for AllStarLink
    ├── samples/       # Configuration file samples
├── include/           # Shared header files
├── res/               # Asterisk resource modules
├── tests/             # Automated test suite
└── utils/             # Helper utilities and tuning tools
```

# Debugging and Submitting Bugs

Feel free to open an issue for *any* trouble you might be experiencing with these modules. Please try to adhere to the following when submitting bugs:

- Enable debug to reproduce the issue. You can do this by running `core set debug 5 app_rpt` (less than or greater than 5 depending on the issue and how chatty the debug log level is). You can also enable debug all the time in `asterisk.conf`. To get debug output on the CLI, you will need to add the `debug` level to the `console => ` log file in `logger.conf`. A debug log from the CLI in the seconds leading immediately up to the issue should be provided.

- For segfault issues, a backtrace is needed. Use `ast_coredumper` to get a backtrace and post the relevant threads from `full.txt` (almost always Thread 1): https://wiki.asterisk.org/wiki/display/AST/Getting+a+Backtrace (you can also run `phreaknet backtrace` - make sure to adjust the paste duration from 24 hours if you link the paste link)

- Describe what led up to the issue and how it can be reproduced on our end.

- Any other context that might be helpful in fixing the issue.

Thank you!

# Development

## First-time Setup

### Build
Build instructions can be found in the asl3-asterisk repo: https://github.com/AllStarLink/asl3-asterisk/tree/develop/docs

### Coding Standards
- Follow existing code style patterns
- All commits must pass clang-format checks
- Run `codespell` before submitting changes
- Write descriptive commit messages explaining *why* changes are made

## Pre-commit Hooks

Install necessary tools:

`sudo apt install clang-format codespell`

After installing clang-format and codespell, set up the pre-commit workflow.

From the top level project directory, execute:

`./.dev/install-hooks`

This will enable coding standards to be checked locally on each commit.

## Contributing Pull Requests
1.  Fork this repository and create a feature branch
2.  Make focused, logical commits with clear descriptions
3.  Ensure pre-commit hooks pass locally
4.  Test your changes with the appropriate test cases
5.  Open a Pull Request describing the change and motivation
6.  Address any review feedback
7.  Once reviewed, the development team will merge the pull request 

# Installing

Install from source instructions can be found in the asl3-asterisk repo: https://github.com/AllStarLink/asl3-asterisk/tree/develop/docs

## Automatic Installation

Updated instructions are in the ASL3-Manual repo at https://github.com/AllStarLink/ASL3-Manual/blob/main/docs/user-guide/install.md
