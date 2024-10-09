#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string>
#include <unistd.h>

using namespace std;

bool acquireLock(const std::string &filepath, int &fd)
{
    fd = open(filepath.c_str(), O_RDWR);
    if (fd == -1)
    {
        return false;
    }
    if (flock(fd, LOCK_EX) == -1)
    {
        return false;
    }
    return true;
}

bool releaseLock(int &fd)
{
    if (flock(fd, LOCK_UN) == -1)
    {
        return false;
    }
    close(fd);
    return true;
}
