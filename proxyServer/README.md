HTTP Proxy Server in C
Project Overview
This project is a multithreaded HTTP proxy server built in C that listens on port 8080 by default. It efficiently manages communication between clients and remote servers, supports a caching mechanism for faster response times, and ensures robust memory management.

Features
Multithreading: Handles multiple client requests simultaneously, enhancing efficiency and performance.
Request Parsing: Parses incoming HTTP requests and forwards them to the appropriate remote server.
Caching Mechanism: Stores frequently accessed data in a cache, reducing load times and improving response speed.
Memory Optimization: Incorporates optimized memory allocation and rigorous error handling for reliable performance.
Setup
Setup on a New Windows System
Install WSL (Windows Subsystem for Linux):

Open PowerShell as an administrator and run the following command to install WSL and set it up with Ubuntu:

powershell
wsl --install

Restart your computer if prompted.
Open Command Prompt (cmd):

Press Win + R, type cmd, and press Enter to open the Command Prompt.
Launch WSL:

In Command Prompt, run:

cmd
wsl

This will launch the WSL terminal with the default Ubuntu distribution inside the Command Prompt.
Install Dependencies:

Inside WSL, update the package lists and install essential tools:

bash
sudo apt update
sudo apt install build-essential

Clone the Repository:

Still inside WSL, clone the project repository:

bash
git clone <repository_url>

Navigate to the Project Directory:

bash
cd <project_directory>

Build the Project Using the Makefile:

To compile the project, run:

bash
make

Run the Proxy Server:

After building the project, a new file called proxy will be created. You can start the proxy server with:

bash
./proxy <port_number>

Replace <port_number> with the desired port number for the proxy server.
