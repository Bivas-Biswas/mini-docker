# Mini Docker 📦

A lightweight container runtime written in C that leverages modern Linux kernel features like namespaces and cgroups v2 to create isolated shell environments. This project demonstrates the core principles of containerization by directly using kernel features, without relying on high-level runtimes like Docker or LXC.

## Overview

`mini-container` spawns a command shell (e.g., `/bin/sh`) within a sandboxed environment. This environment is isolated from the host system using several Linux namespaces:

  * **Mount (mnt):** Provides an isolated view of the filesystem hierarchy.
  * **UTS:** Allows for a custom hostname and domain name.
  * **PID:** Isolates the process ID number space.
  * **Network (net):** Gives the container its own private network stack.

The runtime also configures a **network bridge** and a virtual Ethernet (`veth`) pair to allow seamless network communication between the host and the container. Resource control is managed using **cgroups v2**.

-----

## Features ✨

  * **Portable Root Filesystem:** Uses an Alpine Linux base for a complete and functional user space.
  * **Custom Hostname & IP:** Assign a unique hostname and a static IP address upon launch.
  * **Process & Network Isolation:** Processes and network ports inside the container are separated from the host.
  * **Bridged Networking:** Automatically connects the container to a host bridge for easy network access.
  * **Resource Limiting with Cgroups v2:** Control container resources like memory using modern cgroup interfaces.

-----

## Build and Run Guide

### 1\. Prepare the Root Filesystem

First, create a directory for the container's root filesystem (`rootfs`) and extract the Alpine Linux tarball into it.

```bash
# Create the rootfs directory
mkdir rootfs

# Download and extract the Alpine minirootfs
# (Update the version number as needed)
wget https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/x86_64/alpine-minirootfs-3.19.1-x86_64.tar.gz
tar -xvf alpine-minirootfs-3.19.1-x86_64.tar.gz -C ./rootfs
```

### 2\. Compile the Container Runtime

Use the provided `Makefile` to build the main `container` executable.

```bash
make
```

This will create the binary at `./build/container`.

-----


## Demonstration 1: Client-Server Networking 🌐

This demo shows how to run a server inside the container that a client on the host can connect to.

### 1\. Compile the Test Programs

```bash
# Compile the client for the host machine
gcc client.c -o client

# Compile the server as a static binary for Alpine
gcc server.c -static -o shared_folder/server
```

### 2\. Place the Server in the Root Filesystem

```bash
# Copy the server binary into the shared directory in the rootfs
cp shared_folder/server rootfs/var/shared_folder/
```

### 3\. Launch the Container and Run the Server

```bash
sudo ./build/container ./rootfs mini-container-1 192.168.1.2 /bin/sh
```

From *inside the container's shell*, run the server:

```shell
# You are now inside the container
cd /var/shared_folder
./server 8080
```

### 4\. Connect from the Host

Open a **new terminal on your host machine** and run the client.

```bash
./client 192.168.1.2 8080
```

----

## Demonstration 2: Cgroup Memory Limits 🧠

This demo shows how to limit the container's memory usage and verify that the limit is enforced.

### 1\. Compile the Memory-Hungry Program

The `hungry` program attempts to allocate a large amount of memory. We compile it as a static binary to ensure it runs on Alpine Linux.

```bash
# Create a folder to hold the compiled binary
mkdir shared_folder/

# Compile the hungry program as a static binary
g++ hungry.cpp --static -o shared_folder/hungry
```

### 2\. Place the Program in the Root Filesystem

Copy the compiled binary into a directory inside the container's `rootfs`.

```bash
# Create a directory inside the rootfs
mkdir -p rootfs/var/shared_folder/

# Copy the hungry binary into it
cp shared_folder/hungry rootfs/var/shared_folder/
```

### 3\. Launch the Container and Run the hungry process

```bash
sudo ./build/container ./rootfs mini-container-1 192.168.1.2 /bin/sh

cd var/shared_folder/ && ./hungry

```

The process will be terminated by the kernel's OOM (Out-Of-Memory) killer because the memory limt is 10MB and swap.max is set 0, and you will see the following output:
```
Killed
```
This is the expected result and successfully demonstrates that the cgroup memory limit is being enforced on the container. 

-----

#### Resume Points:

- Developed a container runtime in C/C++ from scratch, leveraging Linux namespaces (PID, NET, MNT, UTS) and cgroups v2 to create fully isolated and resource-constrained application environments.

- Engineered a bridged virtual network using veth pairs to establish seamless L2 connectivity between the host and container, enabling standard client-server communication across isolated network stacks.

#### Elevator Pitch:

> I wanted to go deeper than just using containers, so I built my own runtime in C++ to master the low-level details. My main focus was the networking stack. I engineered a bridged virtual network from the ground up using veth pairs, giving each container its own isolated network. To prove it all worked, I successfully ran a client-server application, with the client on the host connecting to a server running inside the container, demonstrating a solid grasp of Linux virtual networking.
