//
// fileWriter.hpp
//
#ifndef __FILE_WRITER_H__
#define __FILE_WRITER_H__

#include <string>

//
// Helper class to write log file
//
class FileWriter
{
public:
    FileWriter() = default;
    ~FileWriter() { if(IsValid()) { CloseFile(); } }

    bool OpenFile(const std::string& fileName, bool append = true);
    size_t WriteFile(const std::string& buf);
    bool TruncateFile(size_t size);
    bool SetFilePermission(mode_t perm);
    void CloseFile();

    // Preserve sparseness support
    size_t WriteFile(const std::string& buf, off_t offset);

    bool IsValid() { return mErrMsg.empty(); }
    const std::string& GetFileName() { return mFileName; }
    const std::string& GetError() { return mErrMsg; }
    size_t GetFileSize() { return mFileSize; }

protected:
    std::string mErrMsg;

private:
    std::string mFileName;
    int mFd{-1};
    size_t mFileSize{0};
};

#endif // __FILE_WRITER_H__

