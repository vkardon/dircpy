//
// main.cpp
//
#include <string.h>     // strcpy()
#include <limits.h>     // PATH_MAX
#include <libgen.h>     // dirname()
#include <iostream>     // std::cout
#include "dirCopy.h"

#define ERRORMSG(msg) std::cout << "[ERROR] " << __func__ << ": " << msg << std::endl;
#define OUTMSG(msg) std::cout << msg << std::endl;

int main(int argc, const char* argv[])
{
    if(argc < 3)
    {
        std::cout << "Usage: copy <source> <destination> <read_block_size (optional)>" << std::endl;
        return 0;
    }

    const char* srcName = argv[1];
    const char* dstName = argv[2];
    size_t sparseBlockSize = (argc > 3 ? atoi(argv[3]) : 0);

    // For simplicity, make sure that destination directory is not a sub-directory of source directory
    char buf[PATH_MAX + 1] {};
    strcpy(buf, dstName);
    std::string destDirName = dirname(buf);

//    std::cout << "destDir=" << destDir << "-->destDirName=" << destDirName << std::endl;

    if(destDirName == srcName)
    {
        ERRORMSG("Cannot copy the source directory/file into itself");
        return 1;
    }

    // Make destination directory based on source directory name
    strcpy(buf, srcName);
    std::string destDir = std::string(dstName) + "/" + basename(buf);

    OUTMSG("Copy from :" << srcName);
    OUTMSG("Copy to: " << destDir);
    OUTMSG("Sparse files read block size: " << sparseBlockSize);

    // Copy source directory into destination directory
    DirCopy dirCopy(12); // Use 12 threads
    if(!dirCopy.Copy(srcName, destDir, sparseBlockSize))
    {
        ERRORMSG("srcName=" << srcName << ", error '" << dirCopy.GetError() << "'");
        return 1;
    }

    return 0;
}

