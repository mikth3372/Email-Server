#ifndef FLOCKMANAGER_H
#define FLOCKMANAGER_H

#include <string>

bool acquireLock(const std::string &filepath, int &fd);
bool releaseLock(int &fd);

#endif