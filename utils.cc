#include <unistd.h>
#include <ctype.h>
#include <string.h>
#include <iostream>
#include <arpa/inet.h>
#include <sys/stat.h>
#include <vector>
#include <algorithm>

bool do_read(int fd, char *buf, int len)
{
    int rcvd = 0;
    while (rcvd < len)
    {
        int n = read(fd, &buf[rcvd], len - rcvd);
        if (n < 0)
            return false;
        rcvd += n;
    }
    return true;
}

bool do_write(int fd, char *buf, int len)
{
    int sent = 0;
    while (sent < len)
    {
        int n = write(fd, &buf[sent], len - sent);
        if (n < 0)
            return false;
        sent += n;
    }
    return true;
}

void sendResponse(std::string &response, int comm_fd)
{

    unsigned short response_len = response.size();
    // unsigned short wlen = htons(response_len);
    // do_write(comm_fd, (char *)&wlen, sizeof(wlen));
    do_write(comm_fd, const_cast<char *>(response.c_str()), response_len);
    // clear response
    // response.clear();
}

bool validateEmail(std::string email)
{
    // need to check if email contains @
    if (email.find("@localhost") == std::string::npos)
    {
        return false;
    }

    return true;
}

bool fileExists(const std::string &name)
{
    struct stat buffer;
    return (stat(name.c_str(), &buffer) == 0);
}

bool isValidRecipient(const std::string &email, const std::string &directory, std::vector<std::string> &recipients, std::string usecase)
{
    std::cout << "Email in valid is " << email << std::endl;
    std::string username = email.substr(email.find("<") + 1, email.find("@") - (email.find("<") + 1));
    std::cout << username << std::endl;
    std::cout << std::endl;
    std::cout << "Recipients are" << std::endl;
    for (int i = 0; i < recipients.size(); i++)
    {

        std::cout << recipients[i] << std::endl;
    }

    if (std::find(recipients.begin(), recipients.end(), username) == recipients.end())
    {
        return false;
    }

    if (usecase == "SMTP" && validateEmail(email) == false)
    {
        return false;
    }

    return true;

    // std::string path = directory + "/" + email;

    // return fileExists(path);
}

void shutdown_client(int comm_fd)
{
    std::string response = "-ERR Server Shutting Down!\n";
    sendResponse(response, comm_fd);
}

void verbose_check(int comm_fd, std::string buffer, bool verbose)
{
    if (verbose)
    {
        fprintf(stdout, "[%d] S: %s", comm_fd, buffer.c_str());
    }
}
