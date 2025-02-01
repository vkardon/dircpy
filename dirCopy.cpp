//
// dirWriter.cpp
//
#include <filesystem>               // std::filesystem
#include <sys/stat.h>               // stat()
#include <string.h>                 // strerror()
#include <limits.h>                 // PATH_MAX
#include <libgen.h>                 // basename()
#include <thread>                   // std::thread
#include <iostream>                 // std::cout
#include "fileReader.h"
#include "fileWriter.h"
#include "dirCopy.h"

bool DirCopy::Copy(const std::string& srcName, const std::string& destName, size_t sparseBlockSize /*=0*/)
{
    // Are we copying a file or a directory?
    struct stat st;
    if(stat(srcName.c_str(), &st) < 0)
    {
        mErrMsg = "stat() failed for srcDir='" + srcName + "': " + strerror(errno);
        return false;
    }

//    std::cout << __func__ << ": From : '" << srcName << "'" << std::endl;
//    std::cout << __func__ << ": To   : '" << destName << "'" << std::endl;
//    std::cout << __func__ << ": Sparse Block : " << sparseBlockSize << " bytes" << std::endl;

    mSparseBlockSize = sparseBlockSize;
    bool res = false;

    // Reset progress. 
    // Note: If copying a directory, then set mProgress negative to block
    // reporting progress until we get complete mTotalDirAndFiles
    mProgress = -1;

    if((st.st_mode & S_IFMT) == S_IFDIR)
    {
        // Make a destination directory (if doesn't exist)
        std::error_code err;
        std::filesystem::create_directories(destName, err);
        if(err)
        {
            mErrMsg = "Failed to make '" + destName + "' directory - " + err.message();
            return 1;
        }

        // Copy directory
        res = CopyDir(srcName, destName);
    }
    else
    {
        // Make file directory (it doesn't exist)
        char buf[PATH_MAX + 1] {};
        strcpy(buf, destName.c_str());
        std::string fileDirName = dirname(buf);

        std::error_code err;
        std::filesystem::create_directories(fileDirName, err);
        if(err)
        {
            mErrMsg = "Failed to make '" + fileDirName + "' directory - " + err.message();
            return 1;
        }

        // Copy file
        res = CopyFile(srcName, destName, true /*updateProgress*/);
    }

    return res;
}

void* DirCopy::OnDirectory(const char* dirName, const char* baseName, void* param)
{
    DirReaderParam* parentDirParam = (DirReaderParam*)param;
    std::string destDir = parentDirParam->destDir + "/" + baseName;

//    std::cout << __func__ << ": srcDir=" << dirName << "/" << baseName << "/" << std::endl;
//    std::cout << __func__ << ": destDir=" << destDir << "/" << std::endl;
//    std::cout << std::endl;

    //std::cout << destDir << "/" << std::endl;

    // Update total Dir/Files count
    {
        std::unique_lock<std::mutex> lock(mProgressMutex);
        mTotalDirAndFiles++;
    }

    // Make destination directory
    std::error_code error;
    std::filesystem::create_directories(destDir, error);
    if(error)
    {
        mAbort = true;
        SetError("Failed to make '" + destDir + "' directory - " + error.message());
        return nullptr;
    }

    // Update saved Dir/Files count and report overall progress
    UpdateProgress();

    // We have new directory now. Create corresponding DirReaderParam
    DirReaderParam* dirParam = new (std::nothrow) DirReaderParam;
    if(!dirParam)
    {
        mAbort = true;
        SetError("Out of memory creating DirReaderParam");
        return nullptr;
    }
    dirParam->destDir = std::move(destDir);

    return dirParam;
}

void DirCopy::OnDirectoryEnd(const char* /*dirName*/, void* param)
{
    if(param)
        delete (DirReaderParam*)param;
}

void DirCopy::OnFile(const char* dirName, const char* baseName, void* param)
{
    DirReaderParam* dirParam = (DirReaderParam*)param;
    std::string srcFile = std::string(dirName) + "/" + baseName;
    std::string destFile = dirParam->destDir + "/" + baseName;

//    std::cout << __func__ << ": srcFile=" << srcFile << std::endl;
//    std::cout << __func__ << ": destFile=" << destFile << std::endl;
//    std::cout << std::endl;

    // Update total Dir/Files count
    {
        std::unique_lock<std::mutex> lock(mProgressMutex);
        mTotalDirAndFiles++;
    }

    // Post copy file request to thread pool
    mTpool.Post([this](const std::string& srcFile, const std::string& destFile)
    {
        if(!CopyFile(srcFile, destFile))
            mTpool.Stop(); // Force other threads to stop

        // Update saved Dir/Files count and report overall progress
        UpdateProgress();
        
    }, srcFile, destFile);
}

bool DirCopy::CopyDir(const std::string& srcDir, const std::string& destDir)
{
    // Start worker threads
    mTpool.Create(mThreadCount);

    // Read directory
    DirReaderParam dirParam { destDir };
    if(!Read(srcDir, &dirParam))
    {
        mTpool.Stop(); // Force threads to stop
    }
    else
    {
        // Done reading directory (mTotalDirAndFiles has a correct max value)
        // Worker threads are still running, but we can start reporting a progress
        std::unique_lock<std::mutex> lock(mProgressMutex);
        mProgress = 0; // Unblock (start) reporting progress
    }

    // Wait for threads to complete
    mTpool.Wait();
    mTpool.Destroy();

    // We should only have errors if we failed
    return mErrMsg.empty();
}

bool DirCopy::CopyFile(const std::string& srcFile, const std::string& destFile, bool updateProgress/*=false*/)
{
//    std::cout << __func__ << ": srcFile=" << srcFile << std::endl;
//    std::cout << __func__ << ": destFile=" << destFile << std::endl;
//    std::cout << __func__ << ": sparseBlockSize=" << sparseBlockSize << std::endl;
//    std::cout << std::endl;

    FileReader reader;
    if(!reader.OpenFile(srcFile))
    {
        SetError("FileReader error '" + reader.GetError() + "'");
        return false;
    }
    reader.SetSparseBlockSize(mSparseBlockSize);

    FileWriter writer;
    if(!writer.OpenFile(destFile))
    {
        SetError("FileWriter error '" + writer.GetError() + "'");
        return false;
    }

    // Read in maxReadSize chanks
    //static constexpr int maxReadSize = 1024 * 1024 * 3; // 3MB
    static constexpr int maxReadSize = 1024 * 128; // 128KB
    std::string buf;

    while(reader.HasMore())
    {
        // Read source file
        off_t dataOffset = reader.ReadFile(buf, maxReadSize);

        // Write destination file
        /*size_t written =*/ writer.WriteFile(buf, dataOffset);
        if(!writer.IsValid())
        {
            SetError("FileWriter error '" + writer.GetError() + "'");
            return false;
        }

//        std::cout << __func__ << ": Offset=" << dataOffset << ": read " << buf.size() << ", written " << written << std::endl;

        // Update file reading/writing progress
        if(updateProgress)
        {
            int progress = (int)(100 * reader.GetReadSize() / reader.GetFileSize());
            if(progress != mProgress)
            {
                mProgress = progress;
//                std::cout << progress << '%' << (mProgress == 100 ? '\n' : ' ') << std::flush;
                std::cout << '\r' << "Progress: " << progress << '%' << (mProgress == 100 ? '\n' : ' ') << std::flush;
            }
        }
    }

    //std::cout << __func__ << ": Read  total: " << reader.GetReadSize() << std::endl;
    //std::cout << __func__ << ": Write total: " << writer.GetFileSize() << std::endl;

    return true;
}

void DirCopy::UpdateProgress()
{
    std::unique_lock<std::mutex> lock(mProgressMutex);

    mSavedDirAndFiles++;

    if(mProgress < 0)
        return;

    int progress = (int)(100 * mSavedDirAndFiles / mTotalDirAndFiles);

    if(progress != mProgress)
    {
        mProgress = progress;
//        std::cout << progress << '%' << (mProgress == 100 ? '\n' : ' ') << std::flush;
        std::cout << '\r' << "Progress: " << progress << '%' << (mProgress == 100 ? '\n' : ' ') << std::flush;
    }
}

void DirCopy::SetError(const std::string& err)
{
    // Note: we only set the first error as most relevant
    std::unique_lock<std::mutex> lock(mErrMsgMutex);
    if(mErrMsg.empty())
        mErrMsg = err;
}

