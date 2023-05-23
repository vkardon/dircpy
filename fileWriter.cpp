//
// fileWriter.cpp
//
#include "fileWriter.h"
#include <string.h>     // strerror()
#include <unistd.h>
#include <fcntl.h>      // open()
#include <sys/stat.h>   // fstat()
#include <iostream>

//
// Log writer implementation
//
bool FileWriter::OpenFile(const std::string& fileName, bool append /*= true*/)
{
    if(fileName.empty())
    {
        mErrMsg = "Invalid (empty) log file name";
        return false;
    }

    mFileName = fileName;

    int flags = O_CREAT | O_RDWR | (append ? O_APPEND : O_TRUNC);
    int mode = 0660;

    int fd = open(mFileName.c_str(), flags, mode);
    if(fd < 0)
    {
        int errNo = errno;
        mErrMsg = "Could not open '" + mFileName + "' because of: ";
        mErrMsg += strerror(errNo);
        return false;
    }

    struct stat st;
    if(fstat(fd, &st) != 0)
    {
        int errNo = errno;
        mErrMsg = "Failed to stat '" + mFileName + "' because of: ";
        mErrMsg += strerror(errNo);
        close(fd);
        return false;
    }

    mFd = fd;
    mFileSize = st.st_size;
    return true;
}

size_t FileWriter::WriteFile(const std::string& buf)
{
    size_t rem = buf.size();    // Bytes remaining to be written
    const void* ptr = buf.c_str();
    size_t written = 0;

    while(rem > 0)
    {
        // more to be written
        ssize_t wrote = write(mFd, ptr, rem);

        if(wrote < 0)
        {
            if(errno == EAGAIN)
                continue;

            int errNo = errno;
            mErrMsg = "Failed to write to '" + mFileName + "' because of: ";
            mErrMsg += strerror(errNo);
            break;  // Unrecoverable error
        }

        // We successfully wrote something
        written += wrote;
        rem -= wrote;                 // Reduce amount to be written
        ptr = ((char*)ptr) + wrote;   // Advance write point
    }

    // Advance written total and file size
    mFileSize += written;
    return written;
}

// Preserve sparseness support
size_t FileWriter::WriteFile(const std::string& buf, off_t offset)
{
    if(offset > (off_t)mFileSize)
    {
        return (TruncateFile(offset) ? WriteFile(buf) : 0);
    }
    else
    {
        return WriteFile(buf);
    }
}

bool FileWriter::TruncateFile(size_t size)
{
    if(mFileSize == size)
        return true; // Already at the right size, nothing to truncate

    while(ftruncate(mFd, size) != 0)
    {
        if(errno != EINTR)
        {
            int errNo = errno;
            mErrMsg = "Failed to truncate '" + mFileName + "' to " + std::to_string(size) + " bytes because of: ";
            mErrMsg += strerror(errNo);
            return false;  // Unrecoverable error
        }
        usleep(10000);
    }

    mFileSize = size; // Update file size
    return true;
}

bool FileWriter::SetFilePermission(mode_t perm)
{
    while(fchmod(mFd, perm) != 0)
    {
        if(errno != EINTR)
        {
            int errNo = errno;
            mErrMsg = "Failed to chmod() '" + mFileName + "' because of: ";
            mErrMsg += strerror(errNo);
            return false;  // Unrecoverable error
        }
        usleep(10000);
    }

    return true;
}

void FileWriter::CloseFile()
{
    mErrMsg.clear();
    mFileName.clear();

    if(mFd > 0)
    {
        close(mFd);
        mFd = -1;
    }

    mFileSize = 0;
}

