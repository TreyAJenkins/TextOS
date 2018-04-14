# Creating a FAT32 disk image


## Create a disk image with DD
```
dd if=/dev/zero of=disk.img bs=1M count=1
```
This will create a 1MB disk image named `disk.img`. To increase the size, change the `count` argument

## Create the partition and partition table
```
$ fdisk disk.img

Command (m for help): n
Partition type
   p   primary (0 primary, 0 extended, 4 free)
   e   extended (container for logical partitions)
Select (default p):

Using default response p.
Partition number (1-4, default 1):
First sector (1-2047, default 1):
Last sector, +sectors or +size{K,M,G,T,P} (1-2047, default 2047):

Created a new partition 1 of type 'Linux' and of size 1023.5 KiB.

Command (m for help): t
Selected partition 1
Partition type (type L to list all types): c
Changed type of partition 'Linux' to 'W95 FAT32 (LBA)'.

Command (m for help): w
The partition table has been altered.
Syncing disks.
```

## Format as FAT32
Attach the partition we just created as `/dev/mapper/loop0p1`
```
sudo kpartx -a disk.img
```
Format the partition as Fat32
```
sudo mkdosfs -F32 -I /dev/mapper/loop0p1
```
Deattach the disk image
```
sudo kpartx -d disk.img
```
