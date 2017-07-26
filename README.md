# wfslib
WFS (WiiU File System) Library and Tools

## Usage

### wfsdump
```
wfsdump --help
```
```
Usage: wfsdump --input <input file> --output <output directory> --otp <opt path> [--seeprom <seeprom path>] [--mlc] [--usb] [--dump-path <directory to dump>] [--verbos]
Allowed options:
  --help                produce help message
  --input arg           input file
  --output arg          ouput directory
  --otp arg             otp file
  --seeprom arg         seeprom file (required if usb)
  --dump-path arg (=/)  directory to dump (default: "/")
  --mlc                 device is mlc (default: device is usb)
  --usb                 device is usb
  --verbos              verbos output
```

### wfs-fuse (Linux only)
```
wfs-fuse --help
```
```
usage: wfs-fuse <device_file> <mountpoint> --otp OTP_PATH [--seeprom SEEPROM_PATH] [--usb] [--mlc]

options:
    --help|-h             print this help message
    --otp|-o PATH         otp file
    --seeprom|-s PATHH    seeprom file
    --usb|-u              device is usb (default)
    --mlc|-m              device is mlc
    -d   -o debug         enable debug output (implies -f)
    -f                    foreground operation
    -s                    disable multi-threaded operation
```

### Example
#### Dump mlc from backup
```
wfsdump --input mlc.full.img --output dump_dir --otp otp.bin --mlc
```

#### Dump USB device under Windows
(Needed to be run with administrator previliges, so run from privileged command line)
```
wfsdump --input \\.\PhysicalDrive3 --output dump_dir --otp otp.bin --seeprom seeprom.bin
```
You need to replace PhsyicalDrive3 with the correct device, you can figure it out with this PowerShell command
```
Get-WmiObject Win32_DiskDrive
```

#### Mount USB device in Linux
```
sudo wfs-fuse /dev/sdb /mnt --otp otp.bin --seeprom seeprom.bin
```

## Build
### Linux
Install the requirements
```
sudo apt-get install g++ make libfuse-dev libboost-system-dev libboost-filesystem-dev libboost-program-options-dev libcrypto++-dev
```
Run the makefile
```
make
```

### Visual Studio
Visual Studio 2015 project file is provided. This project depends on the libraries boost and Crypto++. Configuration of those libraries include path and lib path is required.
