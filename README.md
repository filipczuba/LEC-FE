# Kaleidoscope Compiler

This repository contains an implementation of the Kaleidoscope language, a simple language introduced in the [LLVM Tutorial](https://llvm.org/docs/tutorial/). This project has been made for the Compilers course held at the University of Modena and Reggio Emilia.

## Table of Contents

- [Features](#features)
- [Getting Started](#getting-started)
  - [Prerequisites](#prerequisites)
  - [Installation](#installation)
- [Usage](#usage)
- [Examples](#examples)
- [License](#license)

## Features

- Arithmetic and boolean operations;
- Local and global variable definitions and assignments;
- Control flow constructs (if/else and for loops);
- Function definitions and calls;
- Arrays.

## Getting Started

### Prerequisites

To build and run the Kaleidoscope compiler, you will need the following tools and libraries installed on your system:

- [LLVM](https://llvm.org/) (version 16 has been used for this project.)
- [Clang](https://clang.llvm.org/) (Apple's version 15 has been used for this project.)
- [Bison parser](https://www.gnu.org/software/bison/) (version 3.8.2 has been used for this project.)
- [Flex lexer]() (version 2.6.4 has been used for this project.);

### Installation
1. Modify the Makefile: change the LLVM path to the one appropriate for your system.
2. Clone the repository:

   ```sh
   git clone https://github.com/filipczuba/LEC-FE.git
   cd LEC-FE
3. Run the makefile:

   ```sh
   make

## Usage
To run the compiler (Use -p and -s for parsing and scanning verbose modes):

   ```sh
   ./kcomp sourcefile.k 2> IRfile.ll
  ```

Note: to run your Kaliedoscope code you'll need a C file containing Main and an extern definition of the function(s) contained in your Kaleidoscope source file.


## Licences
This project is licensed under the MIT License.


