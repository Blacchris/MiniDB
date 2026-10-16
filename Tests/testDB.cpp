#include "MiniDB/src/diskmanager.h"
#include <iostream>
#include <string>
#include <system_error>

int main() {

    try {
        std::string path = "/home/grand_marshal/Documents/miniDB/file.db";
        DiskManager manager(path);

        // e.what() returns a const char*, not an exception object
    } catch (const std::system_error& e) {
        std::cerr << "System error: " << e.what() << '\n';
        return 1;

    } catch (const std::runtime_error& e) {
        std::cerr << "Runtime error: " << e.what() << '\n';
        return 1;
    }

    /***  Remember the distinction
    - e.what() — retrieves the error message.
    - std::cerr << e.what() — prints the message.
    - throw; — rethrows the original exception.
    - throw e.what(); — throws a new exception of type const char*.
    ***/
    return 0;
}