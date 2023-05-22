//
// fileReader.hpp
//
#ifndef __FILE_READER_H__
#define __FILE_READER_H__

#include <string>

//
// Helper class to read file
//
class FileReader
{
public:
    FileReader() = default;
    ~FileReader() { if(IsValid()) { CloseFile(); } }

    //
    // Note: Only single ReadFile() per OpenFile() supported
    //
    bool OpenFile(const std::string& fileName, off_t readBeginOffset, off_t readEndOffset /* -1 for EOF */);
    bool OpenFile(const std::string& fileName) { return OpenFile(fileName, 0, -1); } // To read entire file
    void CloseFile();

    off_t ReadFile(/*out*/ std::string& buf, ssize_t maxSize /* -1 for all */, bool sparse=false)
    {
        return (sparse && mMaxSparseBlockSize > 0 ? ReadSparseFile(buf, maxSize) : ReadRegularFile(buf, maxSize));
    }

    bool IsValid() { return mErrMsg.empty(); }
    const std::string& GetFileName() { return mFileName; }
    off_t GetFileSize() { return mFileSize; }
    mode_t GetFileMode() { return mFileMode; }
    const std::string& GetError() { return mErrMsg; }
    void SetError(const std::string& err) { mErrMsg = err; };
    bool HasMore() { return (mFileSize > 0 && mReadSize < (size_t)(mReadEndOffset - mReadBeginOffset)); }

    // Get the hash value of the entire file
    static size_t Checksum(const std::string& fileName);

    //void * GetReadBeginAddr() { return mReadAddr; }     // Read address corresponding to begin offset
    //size_t GetReadMaxSize() { return (mReadEndOffset - mReadBeginOffset); } // Max size to read
    size_t GetReadSize() { return mReadSize; }          // Current read size

    // Preserve sparseness support
    void  SetSparseBlockSize(size_t sparseBlockSize) { mMaxSparseBlockSize = sparseBlockSize; }

private:
    off_t ReadRegularFile(/*out*/ std::string& buf, ssize_t maxSize /* -1 for all */);

    // Preserve sparseness support
    off_t ReadSparseFile(/*out*/ std::string& buf, ssize_t maxSize /* -1 for all */);
    bool IsSparse(void* addr, size_t size);

protected:
    // Class data
    std::string mErrMsg;
    std::string mFileName;
    off_t mFileSize{0};
    mode_t mFileMode{0};

    void* mMapAddr{nullptr};
    size_t mMapLength{0};

    void* mReadAddr{nullptr};
    size_t mReadSize{0};
    off_t mReadBeginOffset{0};
    off_t mReadEndOffset{0};

    // Experimental: preserve sparseness
    size_t mMaxSparseBlockSize{512};
};

#endif // __FILE_READER_H__

