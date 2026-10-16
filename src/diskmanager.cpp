#include "MiniDB/src/diskmanager.h"
#include <iostream>
#include <fcntl.h>
#include <stdexcept> 
#include <sys/stat.h>
#include <unistd.h>
#include <system_error>  // std::system_error, std::generic_category()
#include <cerrno>        // errno 


DiskManager::DiskManager(const string& path) {

    fd_ = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (fd_ == -1) {
        throw std::runtime_error("failed to open file at " + path);
        exit(1);
    }

    struct stat info;

    try {
        if (fstat(fd_, &info) == -1) {
            throw std::system_error(
                errno,
                std::generic_category(),
                "fstat() failed for file descriptor " + std::to_string(fd_)
            );
        }

        off_t file_size = info.st_size;

        if (file_size % PAGE_SIZE != 0) {
            throw std::runtime_error(
                "Invalid database file size: " + std::to_string(file_size) +
                " bytes is not a multiple of page size " +
                std::to_string(PAGE_SIZE) + " bytes"
            );
        }
        num_pages_ = (uint32_t) file_size / PAGE_SIZE;
        std::cout << "number of pages on this file: " << num_pages_ << std::endl;
    } catch (std::system_error& e) {
        close(fd_);
        fd_ = -1;
        throw e.what();
    } catch (std::runtime_error& e) {
        close(fd_);
        fd_ = -1;
        throw e.what();
    }
}

DiskManager::~DiskManager() {
    std::cout << "file closed" << std::endl;
}
// /home/grand_marshal/CPP/MiniDB/src/diskmanager.cpp