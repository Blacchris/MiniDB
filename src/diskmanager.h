#include <stddef.h>
#include <stdint.h>
#include <string>


using namespace std;

class DiskManager {
public:
    static constexpr size_t PAGE_SIZE = 4096;
    using page_id_t = uint32_t;

    explicit DiskManager(const string& path);   // open O_RDWR | O_CREAT
    ~DiskManager();                             // close(fd_)

    void readPage(page_id_t id, char* dest);          // pread
    void writePage(page_id_t id, const char* src);    // pwrite
    page_id_t allocatePage();                         // return num_pages_++
    void sync();                                      // fsync(fd_)

private:
    int fd_;
    page_id_t num_pages_;   // recovered from file size on open


    
};


