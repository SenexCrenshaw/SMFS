# **SMFS - Stream Master File System**

SMFS (Stream Master File System) is a FUSE-based virtual file system that bridges streaming media content from Stream Master with a local file system interface. It enables seamless integration with streaming sources, providing a transparent and intuitive way to access, stream, and manipulate media files directly from a mounted directory.



Part of the 
[![Stream Master Logo](https://raw.githubusercontent.com/SenexCrenshaw/StreamMaster/refs/heads/main/streammasterwebui/public/images/streammaster_logo_small.png)](https://github.com/SenexCrenshaw/StreamMaster)
[**Stream Master**](https://github.com/SenexCrenshaw/StreamMaster) family

[![Join the Stream Master Discord](https://img.shields.io/badge/Join%20Stream%20Master%20Discord-7289DA?style=for-the-badge&logo=discord)](https://discord.gg/gFz7EtHhG2)

[![Support via Patreon](https://img.shields.io/badge/Support%20on-Patreon-orange?style=for-the-badge&logo=patreon)](https://www.patreon.com/user?u=52683080)

## **Features**

- Stream `.ts` files from remote URLs using a ring buffer.
- Support for various file formats like `.m3u`, `.xml`, `.strm`, and `.ts`.
- Configurable file types to display and manage via command-line arguments.
- Dynamically fetch and display directory structures and files from remote sources.
- Persistent storage using a configurable `cacheDir` to save files written to the mount point, ensuring they are retained across SMFS restarts.
- Configurable via a JSON configuration file, allowing flexible setup and management.
- Automatically installs a systemd service with `.deb` packages for seamless startup management.

---

## **Getting Started**

### **Download the Latest Release**

To get started with SMFS, download the latest pre-built release:

[![Get SMFS Releases](https://img.shields.io/github/v/release/SenexCrenshaw/SMFS?label=Download&style=for-the-badge&logo=github)](https://github.com/SenexCrenshaw/SMFS/releases)

### **Install SMFS**

#### For Debian-based Systems:

```bash
sudo dpkg -i smfs_<version>.deb
sudo systemctl enable smfs
sudo systemctl start smfs
```

#### For RPM-based Systems:

```bash
sudo rpm -i smfs-<version>.rpm
sudo systemctl enable smfs
sudo systemctl start smfs
```

---

## **Configuration**

SMFS can be customized using a JSON configuration file located at `/etc/smfs/smconfig.json`. Below is an example configuration:

```json
{
    "host": "localhost",
    "port": "7095",
    "apiKey": "your-api-key",
    "mountPoint": "/mnt/smfs",
    "cacheDir": "/var/lib/smfs/cache",
    "enabledFileTypes": ["xml", "m3u", "ts"],
    "streamGroupProfileIds": "",
    "isShort": true,
    "logLevel": "INFO"
}
```

Alternatively, you can override these settings via command-line arguments:

| Argument                           | JSON Key                | Description                                                                                       | Default Value          |
|------------------------------------|-------------------------|---------------------------------------------------------------------------------------------------|------------------------|
| `--help`                           | N/A                     | Display help message.                                                                             | N/A                    |
| `--debug`                          | N/A                     | Enable debug mode for verbose logging.                                                           | Disabled               |
| `--log-level <level>`              | `logLevel`              | Set log level (`TRACE`, `DEBUG`, `INFO`, `WARN`, `ERROR`, `FATAL`).                              | `INFO`                 |
| `--host <host>`                    | `host`                  | Hostname or IP address of the server.                                                            | `localhost`            |
| `--port <port>`                    | `port`                  | Port number for connecting to the server.                                                        | `7095`                 |
| `--apikey <apikey>`                | `apiKey`                | API key for authentication.                                                                      |                        |
| `--mount <mount_point>`            | `mountPoint`            | Mount point for the FUSE file system.                                                            | `/mnt/smfs`            |
| `--streamGroupProfileIds <ids>`    | `streamGroupProfileIds` | IDs for stream group profiles.                                                                   | `5`                    |
| `--isShort <true/false>`           | `isShort`               | Specify if short mode is enabled.                                                                | `true`                 |
| `--cacheDir <path>`                | `cacheDir`              | Directory for storing cached and user-created files.                                              | `/var/lib/smfs/cache`  |
| `--enable-<filetype>=<true/false>` | `enabledFileTypes`      | Enable or disable specific file types. Supported types: `m3u`, `xml`, `strm`, `ts`.              | `m3u`, `xml`, `ts`     |
| `--config <path>`                  | N/A                     | Path to the configuration file.                                                                  | `/etc/smfs/smconfig.json` |

---

## **Running the Application**

### **Examples**

#### 1. Default Settings:

```bash
./smfs
```

This mounts the file system at `/mnt/smfs` and enables `xml`, `m3u`, and `ts` file types by default.

#### 2. Custom Host and Mount Point:

```bash
./smfs --host 192.168.1.10 --port 8080 --mount /media/streamfs
```

Mounts the file system at `/media/streamfs` and connects to the server at `192.168.1.10:7095`.

#### 3. Using a Custom Configuration File:

```bash
./smfs --config /path/to/myconfig.json
```

Loads settings from the specified configuration file.

#### 4. Debug Mode with Selected File Types:

```bash
./smfs --debug --enable-m3u=false --enable-ts=true
```

Runs in debug mode, disables `.m3u` files, and enables `.ts` files.

#### 5. Minimal Setup with Only `.ts` Files:

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
