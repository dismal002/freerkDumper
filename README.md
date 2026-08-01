# freerkDumper
Free and open source Rockchip firmware dumper written in C. It has been mainly tested on various generic Rockchip TV boxes and photo frames and is no way complete and contributions are welcome!

## Usage
```Usage: ./freerkDumper [options]
Device Operations:
  -i, --info         Print device flash and chip information only
  -a, --dump-all     Dump all partitions
  -p, --dump <part>  Dump a specific partition by name (e.g. system)
  -o, --out <dir>    Output directory (default: Output/)
  -y, --physical     Read Physical blocks (528 bytes) instead of Logical
  -m, --mode <mode>  Use specific device mode: msc, loader, maskrom
  -s, --scan         Scan for all connected RockChip devices

Image Operations:
  --unpack <file>    Unpack RockChip image file (KRNL/PARM/RKFP/bootloader)
  --pack <file>      Pack files into RockChip image format
  --encrypt          Encrypt with RC4 when packing
  --detect <file>    Detect image format and show information
  --verify <file>    Verify image signatures and checksums

  -h, --help         Show this help message
```

