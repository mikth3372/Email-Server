
#include "filemanager.h"
#include <dirent.h>
#include <iostream>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "smtp.h"
#include <sys/stat.h>
#include <unordered_map>
#include "filemanager.h"
#include <memory>
#include <unistd.h>
#include <vector>
#include <sys/socket.h>
#include "utils.h"
#include "flockmanager.h"
#include "mailboxdata.h"

using namespace std;

void extractRecipients(const std::string &directory, std::vector<std::string> &recipients)
{

    DIR *dir = opendir(directory.c_str());

    if (dir == NULL)
    {
        return;
    }

    struct dirent *ent;
    std::cout << "Extracting recipients" << std::endl;

    while ((ent = readdir(dir)) != NULL)
    {
        std::cout << "Taking filenames" << std::endl;
        std::string filename = ent->d_name;
        std::cout << filename << std::endl;

        // Skip "." and ".." entries
        if (filename != "." && filename != "..")
        {
            filename = filename.substr(0, filename.size() - 5);

            recipients.push_back(filename);
        }
    }

    closedir(dir);
}

void writeToFile(const std::string &directory, const std::string &email, const std::string &data, const std::unordered_map<std::string, pthread_mutex_t *> &rcptMutexes, const std::string &mail_from)
{
    std::cout << "Writing to file" << std::endl;
    std::string username = email.substr(0, email.find("@"));
    std::cout << "Username is " << username << std::endl;
    pthread_mutex_lock(rcptMutexes.at(username));
    std::string filename = directory + "/" + email.substr(0, email.find("@")) + ".mbox";
    std::cout << "Opening file " << filename << std::endl;
    FILE *file = fopen(filename.c_str(), "a");
    std::cout << "Filename is sad " << filename << std::endl;

    if (file == NULL)
    {
        pthread_mutex_unlock(rcptMutexes.at(username));
        return;
    }

    fprintf(file, "From: %s", mail_from.c_str());
    // add date to file
    time_t now = time(0);
    char *dt = ctime(&now);
    fprintf(file, "%s", dt);

    cout << "Data size is " << data.size() << endl;
    // for (int i = 0; i < data.size(); i++)
    // {
    //     std::cout << data[i] << std::endl;
    fprintf(file, "%s", data.c_str());
    // }

    fclose(file);
    pthread_mutex_unlock(rcptMutexes.at(username));
}

std::string lockMailBox(std::shared_ptr<EmailSession> &emailSession, const std::string &directory, int &fd, std::unordered_map<std::string, pthread_mutex_t *> &rcptMutexes)
{
    std::string filename = directory + "/" + emailSession->username + ".mbox";
    std::cout << "Locking mailbox " << filename << std::endl;
    fd = open(filename.c_str(), O_RDWR);
    if (fd == -1)
    {
        return "-ERR no such mailbox\r\n";
    }
    if (pthread_mutex_trylock(rcptMutexes[emailSession->username]) != 0)
    {
        emailSession->prevState = "";
        // close(fd);
        return "-ERR unable to lock maildrop\r\n";
    }
    else if (flock(fd, LOCK_EX) == -1)
    {
        emailSession->prevState = "";
        return "-ERR unable to lock maildrop\r\n";
    }
    emailSession->prevState = "authenticated";
    return "+OK " + emailSession->username + "'s maildrop locked and ready\r\n";
}

void receiving_mail(std::shared_ptr<EmailSession> &emailSession, const std::string &directory, std::unordered_map<std::string, pthread_mutex_t *> &rcptMutexes, bool verbose)
{
    char tempBuffer[1024];
    ssize_t bytes_received = recv(emailSession->comm_fd, tempBuffer, sizeof(tempBuffer) - 1, 0);
    if (bytes_received <= 0)
    {
        return;
    }
    tempBuffer[bytes_received] = '\0';
    string temp(tempBuffer, bytes_received);
    fprintf(stdout, "temp is %s\n", temp.c_str());
    emailSession->data.append(temp);

    string pattern = "\r\n.\r\n";
    string pattern2 = ".\r\n";
    // size_t found = temp.find(pattern);
    // read a character at a time and check whether the

    cout << "Temp is " << temp << endl;
    if (emailSession->data.find(pattern) != string::npos)
    {
        emailSession->data_incoming = false;
        emailSession->done = true;
        int foundPos = emailSession->data.find(pattern);
        emailSession->data = emailSession->data.substr(0, foundPos + 2);
        // temp = emailSession->data[emailSession->data.size() - 1];
        // if (temp.find(pattern) != string::npos)
        // {
        //     emailSession->data.pop_back();
        //     emailSession->data.push_back(temp.substr(0, temp.find(pattern)));
        // }
        // else
        // {
        //     emailSession->data.pop_back();
        // }
        // emailSession->data.push_back("\r\n");

        // emailSession->data.push_back(temp.substr(0, found2));
        // fprintf(stdout, "Inside found %s\n", data[data.size() - 1].c_str());

        // acquire flock lock on all mailboxes first, if anyone fails, release all and return error
        // do it one by one rather than acquiring all of them
        // vector<int>
        //     locks(emailSession->recipients.size(), -1);

        // for (int i = 0; i < emailSession->recipients.size(); i++)
        // {
        //     string email = emailSession->recipients[i];
        //     string filename = directory + "/" + email.substr(0, email.find("@")) + ".mbox";
        //     bool lock = acquireLock(filename, locks[i]);
        //     if (!lock)
        //     {
        //         for (int j = 0; j < i; j++)
        //         {
        //             releaseLock(locks[j]);
        //         }
        //         string response = "-ERR unable to lock maildrop\r\n";
        //         verbose_check(emailSession->comm_fd, response, verbose);
        //         sendResponse(response, emailSession->comm_fd);
        //         return;
        //     }
        // }

        for (int i = 0; i < emailSession->recipients.size(); i++)
        {
            string email = emailSession->recipients[i];
            string filename = directory + "/" + email.substr(0, email.find("@")) + ".mbox";
            // cout << "Filename is " << filename << endl;
            // std::cout << "Filename is " << filename << std::endl;
            // pthread_mutex_lock(&rcptMutexes[email]);
            int fd;
            acquireLock(filename, fd);
            writeToFile(directory, email, emailSession->data, rcptMutexes, emailSession->mail_from);
            releaseLock(fd);
            // pthread_mutex_unlock(&rcptMutexes[email]);
            // releaseLock(locks[i]);
        }
        string response = "250 OK\r\n";
        verbose_check(emailSession->comm_fd, response, verbose);
        sendResponse(response, emailSession->comm_fd);
        // emailSession->hello_got = false;
        emailSession->mail_from_got = false;
        emailSession->is_recipient = false;
        emailSession->recipients.clear();
        emailSession->data.clear();
    }
}

void readMailBox(const std::string &mailbox, shared_ptr<MailboxData> &mailboxData)
{
    FILE *file = fopen(mailbox.c_str(), "r");
    cout << "Opening mailbox: " << mailbox << endl;

    if (file == NULL)
    {
        return;
    }
    char *line = NULL;
    size_t len = 0;
    ssize_t read;
    int i = 1;
    std::string lineStr;
    cout << "Reading mailbox" << endl;
    read = getline(&line, &len, file);
    if (read == -1)
    {
        return;
    }
    lineStr = std::string(line);
    cout << "Line is " << lineStr << endl;
    while (true)
    {
        if (lineStr.find("From: <") != std::string::npos)
        {

            size_t emailStart = lineStr.find("From: <") + 7;
            size_t emailEnd = lineStr.find('>', emailStart);
            std::string sender = lineStr.substr(emailStart, emailEnd - emailStart);
            std::string date = lineStr.substr(emailEnd + 1);

            mailboxData->emailData.push_back(::make_pair(sender, date));

            mailboxData->numEmails++;
            // read till I encounter next From:<, then calculate the size of the email and write it to the mailboxdata object
            read = getline(&line, &len, file);

            if (read == -1)
            {
                break;
            }

            std::string lineStr2(line);
            ssize_t size = 0;
            std::string content = "";

            while (lineStr2.find("From: <") == std::string::npos)
            {
                std::cout << "LineStr2 is " << lineStr2 << std::endl;

                size += read;
                content += lineStr2;
                read = getline(&line, &len, file);
                cout << "Read is " << read << endl;
                if (read == -1)
                {
                    break;
                }
                lineStr2 = std::string(line);
            }
            mailboxData->mailSizes.push_back(size);
            mailboxData->mail.push_back(content);
            mailboxData->mailIds.push_back(std::to_string(i));
            i++;
            lineStr = lineStr2;
        }
        else
        {
            break;
        }
    }
    fclose(file);
    return;
}

void writeNewMailBox(std::shared_ptr<MailboxData> &mailboxData, std::shared_ptr<EmailSession> &emailSession, const std::string &directory)
{
    string mailbox = directory + "/" + mailboxData->username + ".mbox";
    cout << "directory is " << directory << endl;
    string temp = directory + "/" + "temp" + ".mbox";

    int fileDescriptor = open(temp.c_str(), O_CREAT | O_RDWR, 0666);
    if (fileDescriptor == -1)
    {
        std::cerr << "Error: Could not create or open the temp file." << std::endl;
        return;
    }
    if (fileDescriptor == -1)
    {
        return;
    }

    for (size_t i = 0; i < mailboxData->mailIds.size(); i++)
    {

        if (mailboxData->deletedEmails.find(to_string(stoi(mailboxData->mailIds[i]) - 1)) != mailboxData->deletedEmails.end())
        {
            continue;
        }
        cout << "Writing email to file" << mailboxData->emailData[i].first << endl;
        std::string line = "From: <" + mailboxData->emailData[i].first + ">" + mailboxData->emailData[i].second;
        if (write(fileDescriptor, line.c_str(), line.size()) == -1)
        {
            std::cerr << "Error: Failed to write to the temp file." << std::endl;
            close(fileDescriptor);
            return;
        }
        if (write(fileDescriptor, mailboxData->mail[i].c_str(), mailboxData->mail[i].size()) == -1)
        {
            std::cerr << "Error: Failed to write email content to the temp file." << std::endl;
            close(fileDescriptor);
            return;
        }
        // const char *newline = "\n";
        // if (write(fileDescriptor, newline, 1) == -1)
        // {
        //     std::cerr << "Error: Failed to write newline to the temp file." << std::endl;
        //     close(fileDescriptor);
        //     return;
        // }
    }

    if (unlink(mailbox.c_str()) == -1)
    {
        std::cerr << "Error in deleting old file" << std::endl;
        return;
    }

    if (rename(temp.c_str(), mailbox.c_str()) == -1)
    {
        std::cerr << "Error in renaming temp file" << std::endl;
        return;
    }

    close(fileDescriptor);
    return;
}