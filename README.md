# wfslib
WFS (WiiU File System) Library and Tools

## Status
[![Build status](https://ci.appveyor.com/api/projects/status/i6yjihammn0d053m/branch/master?svg=true)](https://ci.appveyor.com/project/koolkdev/wfslib/branch/master)

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

### wfs-file-injector
Change the content of files in wfs image. The injected file size must be smaller than the allocated size on the disk.
**WARNING: May corrupt the file system, Use at your own risk. Make sure to backup the wfs image.**
```
Usage: wfs-file-injector --image <wfs image> --inject-file <file to inject> --inject-path <file path in wfs> --otp <opt path> [--seeprom <seeprom path>] [--mlc] [--usb]
Allowed options:
  --help                produce help message
  --image arg           wfs image file
  --inject-file arg     file to inject
  --inject-path arg     wfs file path to replace
  --otp arg             otp file
  --seeprom arg         seeprom file (required if usb)
  --mlc                 device is mlc (default: device is usb)
  --usb                 device is usb
```

### wfs-fuse (Linux only)
```
wfs-fuse --help
```
```
usage: wfs-fuse <device_file> <mountpoint> --otp <otp_path> [--seeprom <seeprom_path>] [--usb] [--mlc] [fuse options]

options:
    --help|-h              print this help message
    --otp <path>           otp file
    --seeprom <path>       seeprom file (required if usb)
    --usb                  device is usb (default)
    --mlc                  device is mlc
    -d   -o debug          enable debug output (implies -f)
    -o default_permissions check access permission instead the operation system
    -o allow_other         allow access to the mount for all users
    -f                     foreground operation
    -s                     disable multi-threaded operation

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

#### Inject rom.zip

```
wfsdump --image usb.img --inject-file rom.zip --inject-path /usr/title/00050000/101c3500/content/0010/rom.zip --otp otp.bin --seeprom seeprom.bin
```

#### Mount USB device in Linux
```
sudo wfs-fuse /dev/sdb /mnt --otp otp.bin --seeprom seeprom.bin -o default_permissions,allow_other
```

## Build
### Linux
Install the requirements
```
sudo apt-get install git g++ make libfuse-dev libboost-dev libboost-system-dev libboost-filesystem-dev libboost-program-options-dev libcrypto++-dev
```
Get the code:
```
git clone https://github.com/koolkdev/wfslib.git
cd wfslib
```
Build
```
make
```

### Visual Studio
Visual Studio 2015 solution file is provided. (The dependencies are Nuget packages)


### Mac OS X
Install Xcode command line tools:
```
xcode-select --install
```
Install brew if required
```
ruby -e "$(curl -fsSL https://raw.github.com/Homebrew/homebrew/go/install)"
```
Install the requirements
```
brew install boost cryptopp
brew tap caskroom/cask
brew cask install osxfuse
```
Get the code:
```
git clone https://github.com/koolkdev/wfslib.git
cd wfslib
```
Modify the makefile:
```
perl -p -i -e 's/-lfuse /-lfuse_ino64 /g' wfs-fuse/Makefile
```
Build
```
make
```
Note: You must provide "-o default_permissions,allow_other" argument for wfs-fuse
