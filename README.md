# **SMFS - Streaming Media File System**

SMFS (Stream Master File System) is a FUSE-based virtual file system that bridges streaming media content from Stream Master with a local file system interface. It enables seamless integration with streaming sources, providing a transparent and intuitive way to access, stream, and manipulate media files directly from a mounted directory.

[![Support via Patreon](https://img.shields.io/badge/Support%20on-Patreon-orange?style=for-the-badge&logo=patreon)](https://www.patreon.com/user?u=52683080)

[![Stream Master Logo](https://raw.githubusercontent.com/SenexCrenshaw/StreamMaster/refs/heads/main/streammasterwebui/public/images/streammaster_logo_small.png)](https://github.com/SenexCrenshaw/StreamMaster)
[**Stream Master**](https://github.com/SenexCrenshaw/StreamMaster)


## **Features**

- Stream `.ts` files from remote URLs using a ring buffer.
- Support for various file formats like `.m3u`, `.xml`, `.strm`, and `.ts`.
- Configurable file types to display and manage via command-line arguments.
- Dynamically fetch and display directory structures and files from remote sources.
- Persistent storage using a configurable `cacheDir` to save files written to the mount point, ensuring they are retained across SMFS restarts.

### **Virtual File System and Stream Master Integration**

- **Cache Directory (`cacheDir`)**: Provides a persistent storage location for both cached files and user-created content. Files written to the mount point are stored in the `cacheDir`, ensuring they remain available even after restarting SMFS. This allows metadata, additional media, or application-specific files to coexist seamlessly with the virtual files from Stream Master.
- **Virtual Files**:
  - `.m3u` and `.xml` files: Provide metadata and playlist information, fetched from Stream Master.
  - `.strm` files: Represent streaming URLs for direct access, containing minimal data to fetch streams dynamically.
  - `.ts` files: Video streams fetched via HTTP and streamed locally with buffering support.
- **Overlay**: Combines virtual files and `cacheDir` storage, ensuring seamless integration of streamed and cached data, alongside user-added files.

---

## **Support**

[![Join the Stream Master Discord](https://img.shields.io/badge/Join%20Stream%20Master%20Discord-7289DA?style=for-the-badge&logo=discord)](https://discord.gg/gFz7EtHhG2)


---

## **Dependencies**

Ensure the following dependencies are installed:

- **FUSE3**: For the FUSE filesystem interface.
- **libcurl**: For streaming data from URLs.
- **GCC/G++**: Compiler for C++17 code.
- **make**: Build tool for compiling the project.

On Ubuntu/Debian-based systems, you can install the dependencies:

```bash
sudo apt update
sudo apt install -y fuse3 libfuse3-dev libcurl4-openssl-dev g++ pkg-config make
```

---

## **Building the Project**

### **Prerequisites**

- A modern **C++ compiler** (GCC or Clang recommended).
- CMake version 3.15 or higher.
- FUSE library (`libfuse-dev` on Linux).
- `libcurl` for HTTP requests (`libcurl4-openssl-dev` on Linux).

### **Build Steps**

1. Clone the repository:

   ```bash
   git clone https://github.com/SenexCrenshaw/SMFS
   cd smfs
   ```

2. Create a build directory and run CMake:

   ```bash
   mkdir build
   cd build
   cmake ..
   ```

3. Compile the project:

   ```bash
   make
   ```

4. The binary `smfs` will be created in the `build` directory.

---

## **Running the Application**

### **Usage**

```bash
./smfs [OPTIONS]
```

### **Arguments**

| Argument                           | Description                                                                                       | Default Value          |
|------------------------------------|---------------------------------------------------------------------------------------------------|------------------------|
| `--help`                           | Display help message.                                                                             | N/A                    |
| `--debug`                          | Enable debug mode for verbose logging.                                                           | Disabled               |
| `--log-level <level>`              | Set log level (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`).                              | `INFO`                 |
| `--host <host>`                    | Hostname or IP address of the server.                                                            | `localhost`            |
| `--port <port>`                    | Port number for connecting to the server.                                                        | `7095`                 |
| `--apikey <apikey>`                | API key for authentication.                                                                      |                        |
| `--mount <mount_point>`            | Mount point for the FUSE file system.                                                            | `/mnt/smfs`            |
| `--streamGroupProfileIds <ids>`    | IDs for stream group profiles.                                                                   | `5`                    |
| `--isShort <true|false>`           | Specify if short mode is enabled.                                                                | `true`                 |
| `--cacheDir <path>`                | Directory for storing cached and user-created files.                                              | `/tmp/smfs_storage`    |
| `--enable-<filetype>=<true|false>` | Enable or disable specific file types. Supported types: `m3u`, `xml`, `strm`, `ts`.              | `m3u`, `xml`, `strm`   |

### **Examples**

#### 1. Default Settings:

```bash
./smfs
```

This mounts the file system at `/mnt/smfs` and enables `xml`, `m3u`, and `strm` file types by default.

#### 2. Custom Host and Mount Point:

```bash
./smfs --host 192.168.1.10 --port 8080 --mount /media/streamfs
```

Mounts the file system at `/media/streamfs` and connects to the server at `192.168.1.10:8080`.

#### 3. Debug Mode with Selected File Types:

```bash
./smfs --debug --enable-m3u=false --enable-ts=true
```

Runs in debug mode, disables `.m3u` files, and enables `.ts` files.

#### 4. Minimal Setup with Only `.ts` Files:

```bash
./smfs --enable-xml=false --enable-strm=false --enable-m3u=false --enable-ts=true
```

Mounts a file system where only `.ts` files are displayed and accessible.

---

## **Example File Structure**

Given the following files on the server:

```
/{SGName}/{SGName}.m3u
/{SGName}/{SGName}.xml
/{SGName}/{VideoName}/{VideoName}.ts
/{SGName}/{VideoName}/{VideoName}.strm
```

When mounted locally with default settings:

```bash
ls /mnt/smfs
```

Output:

```
/TestSG/TestSG.m3u
/TestSG/TestSG.xml
/TestSG/FamilyVideo/FamilyVideo.strm
/TestSG/FamilyVideo/FamilyVideo.ts
```

If run with `--enable-m3u=false`:

```bash
ls /mnt/smfs
```

Output:

```
TestSG
```

---

## **Logging**

Logs are stored at `/var/log/smfs/smfs.log` by default. Ensure the directory exists and has appropriate permissions, or configure a custom location in the code (`Logger::InitLogFile`).

---

## **License**

This project is licensed under the [MIT License](LICENSE).

---

## **Contributing**

Contributions are welcome! To contribute:

1. Fork this repository.
2. Create a new branch: `git checkout -b my-feature`.
3. Commit your changes: `git commit -m "Add new feature"`.
4. Push to the branch: `git push origin my-feature`.
5. Submit a pull request.

