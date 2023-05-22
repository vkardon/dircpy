# dircpy
Preserve file sparseness while copying a file or a directory

This example doesn't use lseek() with SEEK_HOLE/SEEK_DATA to locate next file hole/data.
Instead, it scans a file for a zeroed blocks and treats them as sparsed data. 
