//
// dirReader.cpp
//
#include "dirReader.h"

//
// DirReader implementation
//
#include <dirent.h>
#include <string.h>     // strerror

static int dirsort(const struct dirent** dir1, const struct dirent** dir2)
{
    return -alphasort(dir1, dir2); // Alphabetic order
}

static int dirfilter(const struct dirent* dir)
{
    // Ignore "." and ".."
    if(dir->d_name[0] == '.' && dir->d_name[1] == '\0')
        return 0; // Ignore
    else if(dir->d_name[0] == '.' && dir->d_name[1] == '.' && dir->d_name[2] == '\0')
        return 0; // Ignore
    else
        return 1; // Keep
}

bool DirReader::Read(const char* dirName, void* param)
{
    if(mAbort)
        return false;

    struct dirent** dirlist{nullptr};

    int n = scandir(dirName, &dirlist, dirfilter, dirsort);
    if(n < 0)
    {
        mErrMsg = std::string("Could not scandir '") + dirName + "' because of: ";
        mErrMsg += strerror(errno);
        return false;
    }

    while(!mAbort && n--)
    {
        struct dirent* dir = dirlist[n];

        if(dir->d_type == DT_DIR)
        {
            // Got sub-directory to read
            void* subDirParam = OnDirectory(dirName, dir->d_name, param);
            Read(std::string(dirName) + "/" + dir->d_name, subDirParam);
            OnDirectoryEnd(dirName, subDirParam);
        }
//        else if(dir->d_type == DT_LNK)
//        {
//            // TODO: support for links
//        }
        else
        {
            // Got file
            OnFile(dirName, dir->d_name, param);
        }

        free(dir);
    }

    free(dirlist);

    return !mAbort;
}



