# wfslib
WFS (WiiU File System) Library and Tools

## Usage

### wfsdump
```
wfsdump --help
```
```
Usage: wfsdump --input <input file> --output <output directory> --otp <opt path> [--seeprom <seeprom path>] [--mlc] [--usb] [--dump-path <directory to dump>] [--sector-size 9/11/12] [--verbos]
Allowed options:
  --help                 produce help message
  --input arg            input file
  --output arg           ouput directory
  --otp arg              otp file
  --seeprom arg          seeprom file (required if usb)
  --dump-path arg (=/)   directory to dump (default: "/")
  --mlc                  device is mlc (default: device is usb
  --usb                  device is usb
  --verbos               verbos output
  --sector-size arg (=9) sector log2 size of device. 9 for 512 bytes (default), 11 for 2048 bytes and 12 for 4096 bytes
```

### Example
#### Dump mlc from backup
```
wfsdump --input mlc.full.img --output dump_dir --otp otp.bin --mlc
```
#### Dump USB device under Windows
(Needed to be run with administrator previliges, so run from privileged command line)
```
wfsdump --input \\.\PhysicalDrive3 --output dump_dir --otp otp.bin --seeprom seeprom.bin --sector-size 9
```
You can figure out the correct device id and sector size by running the PowerShell command:
```
Get-Disk | Format-List
```
Look in the outpuf for those lines for your device: (Replace PhysicalDrive3 in the example command with the correct device number)
```
Number             : 3
PhysicalSectorSize : 512
```
For 512 bytes, specify 9 (the default), for 2048 bytes sepcify --sector-size 11, and for 4096 bytes specify --sector-size 12. Flash drives usually use sector size 512 bytes, and HDD usually uses 4096. (Older may use 2048).

#### Dump USB device under Linux
```
sudo wfsdump --input /dev/sdb --output dump_dir --otp otp.bin --seeprom seeprom.bin --sector-size 9
```
You can figure out the sector size with the command
```
sudo fdisk -l
```
Example output:
```
Disk /dev/sdb: 29.8 GiB, 32018268160 bytes, 62535680 sectors
Sector size (logical/physical): 512 bytes / 512 bytes
```

## Build
### Linux
Install the requirements
```
sudo apt-get install libboost-system-dev libboost-filesystem-dev libboost-program-options-dev libcrypto++-dev
```
Run the makefile
```
make
```
### Visual Studio
Visual Studio 2015 project file is provided. This project depends on the libraries boost and Crypto++. Configuration of those libraries include path and lib path is required.
