# DirectX Sample Project

This is a sample DirectX 9 application demonstrating basic rendering and memory manipulation techniques. The project is intended for educational purposes and experimentation with DirectX on Windows.

## Features
- Basic DirectX 9 initialization and rendering.
- Example of memory reading/writing (placeholder functions).
- Custom vector structures for 2D and 3D coordinates.

## Prerequisites
- Windows OS.
- Microsoft DirectX SDK (June 2010) installed (download from the official Microsoft archive if available).
- Dev-C++ with MinGW or Visual Studio.

## Installation
1. Clone the repository:
   ```
   git clone https://github.com/yourusername/DirectXSample.git
   ```
2. Open the project in Dev-C++ or Visual Studio.
3. Configure build settings:
   - Add include path: `-I"path\to\DirectX SDK\Include"`.
   - Add library path: `-L"path\to\DirectX SDK\Lib\x64" -ld3d9 -ld3dx9`.
   - Replace "path\to\DirectX SDK" with your installation directory (e.g., `C:\Program Files (x86)\Microsoft DirectX SDK (June 2010)`).
4. Build the project (`F9` in Dev-C++ or Build in Visual Studio).
5. The resulting DLL will be in the project directory.

## Usage
- Inject the compiled DLL into a compatible process using a tool like Cheat Engine (for educational purposes only).
- Use F1, F2, F5 to toggle features, F4 to exit.

## Contributing
Feel free to fork this repository and submit pull requests. Please add comments in English.

## License
This project is for educational use only. Use at your own risk.

---