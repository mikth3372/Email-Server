#ifndef EMAILSESSION_H
#define EMAILSESSION_H

#include <string>
#include <vector>
#include <memory> // For smart pointers

class EmailSession
{
public:
    bool is_recipient = false;
    std::vector<std::string> recipients;
    int comm_fd;
    bool done = false;
    bool data_incoming = false;
    std::string data;
    std::string mail_from;
    bool hello_got = false;
    bool mail_from_got = false;
    std::string username;
    std::string prevState = "";
    std::string password;

    // Constructor
    EmailSession(int fd) : comm_fd(fd) {}
};

#endif
