#ifndef MAILBOXDATA_H
#define MAILBOXDATA_H

#include <string>
#include <vector>
#include <set>

class MailboxData
{
public:
    std::string username;
    std::string password;
    std::vector<std::string> mail;
    std::vector<std::string> mailIds;
    std::vector<int> mailSizes;
    std::set<std::string> deletedEmails;
    std::vector<std::pair<std::string, std::string>> emailData;
    int numEmails;
};

#endif