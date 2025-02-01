//
// dirCopy.h
//
#ifndef __DIR_COPY_H__
#define __DIR_COPY_H__

#include "dirReader.h"
#include "threadPool.h"
#include <mutex>

class DirCopy : public DirReader
{
public:
    DirCopy(int threadCount=4) : mThreadCount(threadCount) {}
    virtual ~DirCopy() = default;

    bool Copy(const std::string& srcDir, const std::string& destDir, size_t sparseBlockSize=0);

private:
    virtual void* OnDirectory(const char* dirName, const char* baseName, void* param) override;
    virtual void OnDirectoryEnd(const char* /*dirName*/, void* param) override;
    virtual void OnFile(const char* dirName, const char* baseName, void* param) override;

    bool CopyDir(const std::string& srcDir, const std::string& destDir);
    bool CopyFile(const std::string& srcFile, const std::string& destFile, bool updateProgress=false);
    void UpdateProgress();
    inline void SetError(const std::string& err);

    struct DirReaderParam
    {
        std::string destDir;
    };

private:
    size_t mSparseBlockSize{0};
    size_t mSavedDirAndFiles{0};
    size_t mTotalDirAndFiles{0};
    int mProgress{-1};
    std::mutex mProgressMutex;
    std::mutex mErrMsgMutex;
    ThreadPool mTpool;
    int mThreadCount{0};
};

#endif // __DIR_COPY_H__
