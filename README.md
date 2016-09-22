# PyHSCam

A C-based python interface for interacting with Photron high-speed cameras from a Windows host.

## Usage

See `example.py` for sample usage. Python must have an exception in Windows Firewall to detect devices.

## Runtime

The module requires the following files in the project directory to import and use this module. If an essential sdk dll is missing (other than `PDCLIB.dll`), the module will throw a PyHSCam.CamRuntimeError with error code 100.

    ./MyProject.py
    ./PyHSCam.pyd
    ./PDCLIB.dll
    ./dll/D512PCI.dll
    ./dll/D1024PCI.dll
    ./dll/DAPX.dll
    ./dll/...


## Building the Project

### Dependencies

- Visual C++ Build Tools 2015
- Boost 1.61
- Python 3.5
- Photron SDK

The rest of this readme will assume that you extracted boost to `C:\boost\boost_1_61_0` and installed python to `C:\Program Files (x86)\Python35-32\`.

### Preparation

1. Set environment variables:
    1. `BOOST_ROOT` = `C:\boost\boost_1_61_0\`
    2. `Path` += `C:\boost\boost_1_61_0\;`
2. Create new file `C:\Users\myUserName\user-config.jam` with the following contents. Note: `msvc : 14.0` = Visual C++ 2015 compiler. Older compilers may work but haven't been tested.

        using msvc : 14.0 ;
        import toolset : using ;
        using python
            : 3.5
            : "C:\\Program Files (x86)\\Python35-32\\python.exe"
            : "C:\\Program Files (x86)\\Python35-32\\include"
            : "C:\\Program Files (x86)\\Python35-32\\libs"
            ;
3. Extract Photron SDK binaries to appropriate locations to create the following structure:

        ./
        ./PyHSCam.cpp
        ./PDCLIB.dll       # (32 bit only - runtime dependency)
        ./dll/*.dll        # (32 bit only - runtime dependency)
        ./inc/*.h          # (              buildtime dependency)
        ./lib/PDCLIB.lib   # (32 bit only - buildtime dependency)


### Compiling

1. Compile boost for python. In the option `-jX`, replace `X` with a thread count appropriate for your cpu:

        cd C:\boost\boost_1_61_0
        bootstrap
        b2 -jX link=static,shared threading=single,multi toolset=msvc-14.0 --libdir=C:\Boost\lib\i386 install
2. Compile the project. In the project's root directory, run bjam to compile:

        bjam variant=release link=static

### "LINK : fatal error LNK1207: incompatible PDB format"

http://stackoverflow.com/questions/29053172/boost-python-quickstart-linker-errors

Edit the file:

`C:\boost\boost_1_61_0\tools\build\src\tools\msvc.jam`

Change this (lines 1351-1355):

```
generators.register [ new msvc-linking-generator msvc.link.dll :
    OBJ SEARCHED_LIB STATIC_LIB IMPORT_LIB : SHARED_LIB IMPORT_LIB :
    <toolset>msvc <suppress-import-lib>false ] ;
generators.register [ new msvc-linking-generator msvc.link.dll :
    OBJ SEARCHED_LIB STATIC_LIB IMPORT_LIB : SHARED_LIB :
    <toolset>msvc <suppress-import-lib>true ] ;
```

to:

```
generators.register [ new msvc-linking-generator msvc.link.dll :
    OBJ SEARCHED_LIB STATIC_LIB IMPORT_LIB : SHARED_LIB IMPORT_LIB :
    <toolset>msvc ] ;
```

Remove this line (1472):

`toolset.flags msvc.link.dll LINKFLAGS <suppress-import-lib>true : /NOENTRY ;`
