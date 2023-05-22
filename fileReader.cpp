//
// fileReader.cpp
//
#include "fileReader.h"
#include <unistd.h>
#include <string.h>         // strerror()
#include <fcntl.h>          // open()
#include <sys/stat.h>       // fstat()
#include <sys/mman.h>       // mmap()
#include <assert.h>         // assert()
#include <iostream>         // std::cout, std::cerr

//
// Log reader implementation
//
bool FileReader::OpenFile(const std::string& fileName,
                          off_t readBeginOffset,
                          off_t readEndOffset /* -1 for EOF */)
{
    CloseFile(); // Clean up first...just in case if FileReader is re-used

    if(fileName.empty())
    {
        mErrMsg = "Invalid (empty) log file name";
        return false;
    }

    mFileName = fileName;
    mReadBeginOffset = readBeginOffset;
    mReadEndOffset = readEndOffset;

    if(mReadBeginOffset == mReadEndOffset)
    {
        return true; // Nothing to read
    }
    else if(mReadEndOffset != -1 && mReadBeginOffset > mReadEndOffset)
    {
        mErrMsg = "Read begin offset " + std::to_string(mReadBeginOffset) + " is past read end offset "
                  + std::to_string(mReadEndOffset) + " of the file '" + mFileName + "'";
        return false;
    }

    int fd = open(mFileName.c_str(), O_RDONLY);
    if(fd < 0)
    {
        int errNo = errno;
        mErrMsg = "Could not open '" + mFileName + "' because of: ";
        mErrMsg += strerror(errNo);
        return false;
    }

    // Get the file size and then mmap the file into memory.
    struct stat fileStats;
    if(fstat(fd, &fileStats) != 0)
    {
        int errNo = errno;
        mErrMsg = "Could not fstat'" + mFileName + "' because of: ";
        mErrMsg += strerror(errNo);
        close(fd);
        return false;
    }

    // Remember file size & mode
    mFileSize = fileStats.st_size;
    mFileMode = fileStats.st_mode;

    // If the file is empty then we have nothing to map, just return
    if(mFileSize == 0)
        return true;

    // Make sure that read end offset is NOT greater than the file size
    if(mReadEndOffset > mFileSize)
    {
        mErrMsg = "Read end offset " + std::to_string(mReadEndOffset) + " is greater than file size "
                  + std::to_string(mFileSize) + " of the file '" + fileName + "'";
        close(fd);
        return false;
    }

    // Are we going to read to the EOF?
    if(mReadEndOffset < 0)
        mReadEndOffset = mFileSize;

    // Offset for mmap() must be page aligned
    off_t alignedOffset = mReadBeginOffset & ~(sysconf(_SC_PAGE_SIZE) - 1);

    // Get the length of mapping
    size_t mapLength = mReadEndOffset - alignedOffset;

    // Map in the file.
    void* addr = mmap(NULL, mapLength, PROT_READ, MAP_PRIVATE, fd, alignedOffset);
    close(fd); // Can close file now, no longer need it

    // Validate mmap() result
    if(addr == MAP_FAILED)
    {
        // victor test
//        std::cout << "### '" << fileName << "'"
//                << ": fd=" << fd
//                << ": readBeginOffset=" << mReadBeginOffset
//                << ", readEndOffset=" << mReadEndOffset
//                << ", mapLength=" << mapLength
//                << ", alignedOffset=" << alignedOffset << std::endl;

        int errNo = errno;
        mErrMsg = "Could not map '" + mFileName + "' because of: ";
        mErrMsg += strerror(errNo);
        return false;
    }
    mMapAddr = addr;
    mMapLength = mapLength;

    // Get the actual file read address
    mReadAddr = (unsigned char*)mMapAddr + mReadBeginOffset - alignedOffset;

    return true;
}

void FileReader::CloseFile()
{
    mErrMsg.clear();
    mFileName.clear();
    mFileMode = 0;

    // Unmap the file.
    if(mMapAddr && munmap(mMapAddr, mMapLength) != 0)
    {
        // Note: We should treat it as a warning, not an error
        std::cerr << "Failed to unmap '" + mFileName + "' because of: " + strerror(errno) << std::endl;
    }
    mMapAddr = nullptr;
    mMapLength = 0;

    mReadAddr = nullptr;
    mReadSize = 0;
    mReadBeginOffset = 0;
    mReadEndOffset = 0;
}

//
// Note: Only single ReadFile() per OpenFile() supported <--? What does it mean?
//
off_t FileReader::ReadRegularFile(/*out*/ std::string& buf,
                                  ssize_t maxSize /* -1 for all */)
{
    buf.clear();
    size_t readMaxSize = (mReadEndOffset - mReadBeginOffset);

    // End of last read - that is an offset to this new read
    off_t readOffset = mReadBeginOffset + mReadSize;

    if(mReadSize >= readMaxSize)
    {
        // Nothing left to read
    }
    else if(maxSize < 0)
    {
        // Read all at once
        buf.assign((char*)mReadAddr, readMaxSize);
        mReadSize = readMaxSize;
    }
    else
    {
        assert(mReadSize < readMaxSize);
        ssize_t remainingSize = (readMaxSize - mReadSize);
        size_t toRead = (remainingSize > maxSize ? maxSize : remainingSize);

        buf.assign((char*)mReadAddr + mReadSize, toRead);
        mReadSize += toRead;
    }

    return readOffset;
}

// Preserve sparseness support
off_t FileReader::ReadSparseFile(/*out*/ std::string& buf,
        ssize_t maxSize /* -1 for all */)
{
    buf.clear();

    size_t readMaxSize = (mReadEndOffset - mReadBeginOffset);
    if(mReadSize >= readMaxSize)
    {
        return (mReadBeginOffset + mReadSize);
    }

    if(maxSize < 0)
        maxSize = readMaxSize;

    // If maxSize is smaller than mMaxSparseBlockSize, then don't check
    // for sparseness and just read normally
    if((size_t)maxSize < mMaxSparseBlockSize)
    {
        return ReadFile(buf, maxSize);
    }

    size_t remainingSize = readMaxSize - mReadSize;
    size_t sparseBlockSize = 0;
    void* readAddr = nullptr;

    // Skip sparse blocks
    while(remainingSize > 0)
    {
        sparseBlockSize = (remainingSize > mMaxSparseBlockSize ? mMaxSparseBlockSize : remainingSize);

//        std::cout << "01 sparseBlockSize=" << sparseBlockSize
//                << ", mMaxSparseBlockSize=" << mMaxSparseBlockSize
//                << ", remainingSize=" << remainingSize << std::endl;

        readAddr = (unsigned char*)mReadAddr + mReadSize;
        mReadSize += sparseBlockSize; // Consider this block read
        if(!IsSparse(readAddr, sparseBlockSize))
            break;

        // Go to the next block
        remainingSize -= sparseBlockSize;
    }

    if(remainingSize == 0)
    {
        // No data left
        return (mReadBeginOffset + mReadSize);
    }

    // We found un-sparse block (valid data)
    void* beginDataAddr = readAddr;
    size_t dataSize = sparseBlockSize;
    remainingSize -= sparseBlockSize;
    assert(mReadSize < readMaxSize || remainingSize == 0);

    // Keep reading until next sparse block
    while(remainingSize > 0)
    {
        sparseBlockSize = (remainingSize > mMaxSparseBlockSize ? mMaxSparseBlockSize : remainingSize);

//        std::cout << "02 sparseBlockSize=" << sparseBlockSize
//                << ", mMaxSparseBlockSize=" << mMaxSparseBlockSize
//                << ", remainingSize=" << remainingSize << std::endl;

        if(dataSize + sparseBlockSize > (size_t)maxSize)
            break; // No more room for data

        readAddr = (unsigned char*)mReadAddr + mReadSize;
        mReadSize += sparseBlockSize; // Consider this block read
        if(IsSparse(readAddr, sparseBlockSize))
            break;

        // Go to the next block
        dataSize += sparseBlockSize;
        remainingSize -= sparseBlockSize;
    }

    // Get all the data
    assert(dataSize <= (size_t)maxSize);
    buf.assign((char*)beginDataAddr, dataSize);

    // Get data offset
    return ((unsigned char*)beginDataAddr - (unsigned char*)mReadAddr);
}

// Preserve sparseness support
bool FileReader::IsSparse(void* addr, size_t size)
{
    // Treat input bytes as array of longs for a faster performance
    const long* lbuf  = reinterpret_cast<const long*>(addr);
    size_t lsize = size / sizeof(long);

    for(size_t i = 0; i < lsize; i++)
    {
        if(lbuf[i] != 0)
            return false;
    }

    // Check the remaining bytes (if we have any)
    size_t rest = size % sizeof(long);
    if(rest == 0)
        return true; // No  remaining bytes

    const char* buf = reinterpret_cast<char*>(addr) + size - rest;

    for(size_t i = 0; i < rest; i++)
    {
        if(buf[i] != 0)
            return false;
    }

    return true;
}

size_t FileReader::Checksum(const std::string& fileName)
{
    // Open to read entire file
    FileReader reader;
    if(!reader.OpenFile(fileName, 0, -1))
        return 0;  // Failed to open

    if(reader.mFileSize == 0)
        return 0; // The file is empty, nothing to checksum

    assert(reader.mReadAddr != nullptr);

    std::hash<std::string_view> hash;
    return hash(std::string_view((char*)reader.mReadAddr, reader.mFileSize));
}

