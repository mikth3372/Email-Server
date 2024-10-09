#ifndef FILEMANAGER_H
#define FILEMANAGER_H

#include <vector>
#include <string>
#include <unordered_map>
#include "email_session.h"
#include "mailboxdata.h"
#include <memory>

void extractRecipients(const std::string &directory, std::vector<std::string> &recipients);
void writeToFile(const std::string &directory, const std::string &email, const std::string &data, const std::unordered_map<std::string, pthread_mutex_t *> &rcptMutexes, const std::string &mail_from);
std::string lockMailBox(std::shared_ptr<EmailSession> &emailSession, const std::string &directory, int &fd, std::unordered_map<std::string, pthread_mutex_t *> &rcptMutexes);
void receiving_mail(std::shared_ptr<EmailSession> &emailSession, const std::string &directory, std::unordered_map<std::string, pthread_mutex_t *> &rcptMutexes, bool verbose);
void readMailBox(const std::string &mailbox, std::shared_ptr<MailboxData> &mailboxData);
void writeNewMailBox(std::shared_ptr<MailboxData> &mailboxData, std::shared_ptr<EmailSession> &emailSession, const std::string &directory);

#endif