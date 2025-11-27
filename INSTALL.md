# Installing Concordia

## Windows

### Prerequisites
- CMake (3.14 or later)
- A C compiler (Visual Studio / MSVC recommended, or MinGW)

### Build and Install

1. **Configure and Build**
   ```powershell
   mkdir build
   cd build
   cmake ..
   cmake --build . --config Release
   ```

2. **Install**
   You can install to a specific directory (recommended for local development) or to the system Program Files.

   **Option A: Local Install (e.g., to a `dist` folder)**
   ```powershell
   cmake --install . --prefix "../dist" --config Release
   ```
   Then add the `dist/bin` folder to your System PATH.

   **Option B: System Install (Requires Administrator)**
   Open PowerShell as Administrator:
   ```powershell
   cmake --install . --config Release
   ```
   This will typically install to `C:\Program Files (x86)\concordia`.

## Linux / macOS

```bash
mkdir build && cd build
cmake ..
make
sudo make install
```
