#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "utils.h"
#include "echoserver.h"
#include <signal.h>
#include <vector>
#include <algorithm>
#include <openssl/md5.h>
#include "email_session.h"
#include "filemanager.h"
#include "mailboxdata.h"
#include "pop3.h"
#include <sys/file.h>

void computeDigest(char *data, int dataLengthBytes, unsigned char *digestBuffer)
{
  /* The digest will be written to digestBuffer, which must be at least MD5_DIGEST_LENGTH bytes long */

  MD5_CTX c;
  MD5_Init(&c);
  MD5_Update(&c, data, dataLengthBytes);
  MD5_Final(digestBuffer, &c);
}

using namespace std;

const int MAX_CONNECTIONS = 100;
int active_connections = 0;
pthread_mutex_t connectionMutex = PTHREAD_MUTEX_INITIALIZER;
int verbose = 0;
bool server_running = true;
vector<int> connections;
vector<pthread_t> threads;
string directory = "";
std::unordered_map<string, pthread_mutex_t *> rcptMutexes;
vector<string> actual_recipients;
// string const = "\r\n";

void handle_sigint(int sig)
{
  // cout << "Caught signal: " << sig << endl;

  string shutting_down = "-ERR Server shutting down\r\n";
  server_running = false;
  verbose_check(0, shutting_down, verbose);
  for (int i = 0; i < connections.size(); i++)
  {
    sendResponse(shutting_down, connections[i]);
    close(connections[i]);
  }
  for (int i = 0; i < threads.size(); i++)
  {
    pthread_cancel(threads[i]);
  }
  // cout << "Server shutting down " << server_running << endl;
  exit(1);
}

void *worker(void *arg)
{
  int comm_fd = *(int *)arg;
  shared_ptr<EmailSession> emailSession = make_shared<EmailSession>(comm_fd);
  shared_ptr<MailboxData> mailboxData = make_shared<MailboxData>();
  int fd;
  string greeting = "+OK POP3 server ready\r\n";
  if (verbose)
  {
    fprintf(stdout, "[%d] S: %s", comm_fd, greeting.c_str());
  }

  do_write(comm_fd, const_cast<char *>(greeting.c_str()), greeting.size());
  string buffer;
  while (true)
  {

    char buf[1024];
    ssize_t bytes_received = recv(comm_fd, buf, sizeof(buf) - 1, 0);
    if (bytes_received <= 0)
    {
      break;
    }

    buffer.append(buf, bytes_received);

    size_t pos;

    while ((pos = buffer.find("\r\n")) != string::npos)
    {
      if (verbose)
      {
        fprintf(stdout, "[%d] C: %s \n", comm_fd, buffer.substr(0, pos).c_str());
      }
      string input = buffer.substr(0, pos);
      buffer.erase(0, pos + 2);
      string response;
      // std::cout << "input: " << input << std::endl;

      std::transform(input.begin(), input.end(), input.begin(), ::tolower);

      if (input.substr(0, 5) == "user ")
      {
        if (emailSession->prevState != "")
        {
          response = "-ERR User already set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }

        string username = input.substr(5);
        username = username.substr(0, username.find("@"));
        cout << "Username is " << username << endl;

        bool valid = isValidRecipient(username, directory, actual_recipients, "POP3");

        if (valid)
        {
          response = "+OK name is a valid mailbox\r\n";
          emailSession->username = username;
          emailSession->prevState = "user";
        }
        else
        {
          response = "-ERR never heard of mailbox name\r\n";
        }

        verbose_check(comm_fd, response, verbose);
        sendResponse(response, comm_fd);
      }
      else if (input.substr(0, 5) == "pass ")
      {
        if (emailSession->prevState == "pass")
        {
          response = "-ERR User already set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
        }
        else if (emailSession->prevState != "user")
        {
          response = "-ERR User not set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
        }
        else
        {
          string password = input.substr(5);
          if (password != "cis505")
          {
            response = "-ERR invalid password\r\n";
            emailSession->prevState = "";
          }
          else
          {
            response = lockMailBox(emailSession, directory, fd, rcptMutexes);
          }
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          cout << "emailSession->prevState: " << emailSession->prevState << endl;

          if (emailSession->prevState == "authenticated")
          {
            // read the mailbox and get the number of emails considering fd is already locked, create mailboxdata object, read the mailbox and get the number of emails and calculate the size of the emails
            string mailBox = directory + "/" + emailSession->username + ".mbox";

            mailboxData->username = emailSession->username;
            mailboxData->password = emailSession->password;
            readMailBox(mailBox, mailboxData);
            cout << "Mailbox read" << endl;
          }
        }
        continue;
      }
      else if (input.substr(0, 4) == "stat")
      {
        if (emailSession->prevState != "authenticated")
        {
          response = "-ERR User not set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        ssize_t totalSize = 0;
        ssize_t totalEmails = 0;

        cout << "Mail size: " << mailboxData->mailSizes.size() << endl;

        // for (int i = 0; i < mailboxData->mail.size(); i++)
        // {
        //   cout << mailboxData->mailIds[i] << endl;
        // }

        sizeAndCountCalculate(mailboxData, totalSize, totalEmails);

        response = "+OK " + to_string(totalEmails) + " " + to_string(totalSize) + "\r\n";
        verbose_check(comm_fd, response, verbose);
        sendResponse(response, comm_fd);
      }
      else if (input.substr(0, 4) == "uidl")
      {
        if (emailSession->prevState != "authenticated")
        {
          response = "-ERR User not set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        if (input.size() == 5)
        {
          response = "-ERR Invalid Argument\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }

        if (input.size() == 4)
        {
          response = "+OK\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          for (int i = 0; i < mailboxData->mail.size(); i++)
          {
            if (mailboxData->deletedEmails.find(to_string(stoi(mailboxData->mailIds[i]) - 1)) == mailboxData->deletedEmails.end())
            {
              unsigned char buf[MD5_DIGEST_LENGTH];
              computeDigest((char *)mailboxData->mail[i].c_str(), mailboxData->mailSizes[i], buf);
              string hash = "";
              for (int j = 0; j < MD5_DIGEST_LENGTH; j++)
              {
                hash += to_string(buf[j]);
              }
              response = to_string(i + 1) + " " + hash + "\r\n";
              verbose_check(comm_fd, response, verbose);
              sendResponse(response, comm_fd);
            }
          }
          response = ".\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
        }
        else
        {
          string inputdemo = input.substr(4);
          bool isNumber = true;
          // fprintf(stdout, "input : %s\n", inputdemo.c_str());
          for (int i = 1; i < inputdemo.size(); i++)
          {
            // fprintf(stdout, "input[i] : %c\n", inputdemo[i]);
            if (!isdigit(inputdemo[i]))
            {
              response = "-ERR invalid argument\r\n";
              verbose_check(comm_fd, response, verbose);
              sendResponse(response, comm_fd);
              isNumber = false;
              break;
            }
          }
          if (!isNumber)
          {
            continue;
          }

          int email_number = stoi(input.substr(5));
          if (email_number > mailboxData->mail.size() || email_number < 1)
          {
            response = "-ERR no such message, only " + to_string(mailboxData->mail.size()) + " messages in maildrop\r\n";
            verbose_check(comm_fd, response, verbose);
            sendResponse(response, comm_fd);
          }
          else
          {
            if (mailboxData->deletedEmails.find(mailboxData->mailIds[email_number - 1]) != mailboxData->deletedEmails.end())
            {
              response = "-ERR message " + to_string(email_number) + " already deleted\r\n";
              verbose_check(comm_fd, response, verbose);
              sendResponse(response, comm_fd);
            }
            else
            {
              unsigned char buf[MD5_DIGEST_LENGTH];
              computeDigest((char *)mailboxData->mail[email_number - 1].c_str(), mailboxData->mailSizes[email_number - 1], buf);
              string hash = "";
              for (int j = 0; j < MD5_DIGEST_LENGTH; j++)
              {
                hash += to_string(buf[j]);
              }
              response = "+OK " + to_string(email_number) + " " + hash + "\r\n";
              verbose_check(comm_fd, response, verbose);
              sendResponse(response, comm_fd);
            }
          }
        }
      }
      else if (input.substr(0, 4) == "retr")
      {
        if (emailSession->prevState != "authenticated")
        {
          response = "-ERR User not set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        if (input.size() == 4 || input.size() == 5)
        {
          response = "-ERR no such message\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        else
        {
          // fprintf(stdout, "inside retr\n");
          // check whether there is an alphabet or non digit character in the argument
          string inputdemo = input.substr(4);
          bool isNumber = true;
          // fprintf(stdout, "input : %s\n", inputdemo.c_str());
          for (int i = 1; i < inputdemo.size(); i++)
          {
            // fprintf(stdout, "input[i] : %c\n", inputdemo[i]);
            if (!isdigit(inputdemo[i]))
            {
              response = "-ERR invalid argument\r\n";
              verbose_check(comm_fd, response, verbose);
              sendResponse(response, comm_fd);
              isNumber = false;
              break;
            }
          }
          if (!isNumber)
          {
            continue;
          }

          int emailNumber = stoi(input.substr(5));
          if (mailboxData->deletedEmails.find(to_string(emailNumber - 1)) != mailboxData->deletedEmails.end())
          {
            response = "-ERR no such message\r\n";
            verbose_check(comm_fd, response, verbose);
            sendResponse(response, comm_fd);
            continue;
          }
          if (emailNumber > mailboxData->mail.size())
          {
            response = "-ERR no such message\r\n";
            verbose_check(comm_fd, response, verbose);
            sendResponse(response, comm_fd);
            continue;
          }

          response = "+OK " + to_string(mailboxData->mailSizes[emailNumber - 1]) + "\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);

          response = mailboxData->mail[emailNumber - 1];
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);

          response = ".\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
      }
      else if (input.substr(0, 4) == "dele")
      {
        if (emailSession->prevState != "authenticated")
        {
          response = "-ERR User not set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        if (input.size() == 4 || input.size() == 5)
        {
          response = "-ERR no such message\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        else
        {
          string inputdemo = input.substr(4);
          bool isNumber = true;
          // fprintf(stdout, "input : %s\n", inputdemo.c_str());
          for (int i = 1; i < inputdemo.size(); i++)
          {
            // fprintf(stdout, "input[i] : %c\n", inputdemo[i]);
            if (!isdigit(inputdemo[i]))
            {
              response = "-ERR invalid argument\r\n";
              verbose_check(comm_fd, response, verbose);
              sendResponse(response, comm_fd);
              isNumber = false;
              break;
            }
          }
          if (!isNumber)
          {
            continue;
          }
          int messageId = stoi(input.substr(5)) - 1;
          if (messageId >= mailboxData->numEmails)
          {
            response = "-ERR no such message\r\n";
            verbose_check(comm_fd, response, verbose);
            sendResponse(response, comm_fd);
            continue;
          }
          else if (mailboxData->deletedEmails.find(to_string(messageId)) == mailboxData->deletedEmails.end())
          {
            mailboxData->deletedEmails.insert(to_string(messageId));
            response = "+OK message" + input.substr(5) + " deleted\r\n";
            verbose_check(comm_fd, response, verbose);
            sendResponse(response, comm_fd);
            continue;
          }
          else
          {
            response = "-ERR message " + input.substr(5) + " already deleted\r\n";
            verbose_check(comm_fd, response, verbose);
            sendResponse(response, comm_fd);
            continue;
          }
        }
      }
      else if (input.substr(0, 4) == "list")
      {
        if (emailSession->prevState != "authenticated")
        {
          response = "-ERR User not set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        if (input.size() == 5)
        {
          response = "-ERR invalid argument\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }

        if (input.size() == 4)
        {
          ssize_t total = 0;
          ssize_t count = 0;
          sizeAndCountCalculate(mailboxData, total, count);
          response = "+OK " + to_string(count) + " messages (" + to_string(total) + " octets)\r\n";
          sendResponse(response, comm_fd);
          verbose_check(comm_fd, response, verbose);

          for (int i = 0; i < mailboxData->numEmails; i++)
          {
            if (mailboxData->deletedEmails.find(to_string(stoi(mailboxData->mailIds[i]) - 1)) == mailboxData->deletedEmails.end())
            {
              response = mailboxData->mailIds[i] + " " + to_string(mailboxData->mailSizes[i]) + "\r\n";
              sendResponse(response, comm_fd);
              verbose_check(comm_fd, response, verbose);
            }
          }
          response = ".\r\n";
          sendResponse(response, comm_fd);
          verbose_check(comm_fd, response, verbose);
          continue;
        }
        else
        {
          string inputdemo = input.substr(4);
          bool isNumber = true;
          // fprintf(stdout, "input : %s\n", inputdemo.c_str());
          for (int i = 1; i < inputdemo.size(); i++)
          {
            // fprintf(stdout, "input[i] : %c\n", inputdemo[i]);
            if (!isdigit(inputdemo[i]))
            {
              response = "-ERR invalid argument\r\n";
              verbose_check(comm_fd, response, verbose);
              sendResponse(response, comm_fd);
              isNumber = false;
              break;
            }
          }
          if (!isNumber)
          {
            continue;
          }
          int mailId = stoi(input.substr(5)) - 1;

          if (mailId >= mailboxData->numEmails || mailId < 1)
          {
            response = "-ERR no such message, only " + to_string(mailboxData->numEmails - mailboxData->deletedEmails.size()) + " in maildrop\r\n";
            verbose_check(comm_fd, response, verbose);
            sendResponse(response, comm_fd);
            continue;
          }

          if (mailboxData->deletedEmails.find(to_string(mailId)) == mailboxData->deletedEmails.end())
          {
            response = "+OK " + to_string(mailId + 1) + " " + to_string(mailboxData->mailSizes[mailId]) + "\r\n";
          }
          else
          {
            ssize_t totalEmails = 0;
            ssize_t total = 0;
            sizeAndCountCalculate(mailboxData, total, totalEmails);
            response = "-ERR no such message, only " + to_string(totalEmails) + " in maildrop\r\n";
          }
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
      }
      else if (input.substr(0, 4) == "noop")
      {
        if (emailSession->prevState != "authenticated")
        {
          response = "-ERR User not set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        response = "+OK\r\n";
        verbose_check(emailSession->comm_fd, response, verbose);
        sendResponse(response, emailSession->comm_fd);
        continue;
      }
      else if (input.substr(0, 4) == "rset")
      {
        if (emailSession->prevState != "authenticated")
        {
          response = "-ERR User not set\r\n";
          verbose_check(comm_fd, response, verbose);
          sendResponse(response, comm_fd);
          continue;
        }
        mailboxData->deletedEmails.clear();
        ssize_t total = 0;
        totalSizeCalculate(mailboxData, total);
        response = "+OK maildrop has " + to_string(mailboxData->numEmails) + " messages (" + to_string(total) + ")\r\n";

        verbose_check(comm_fd, response, verbose);
        sendResponse(response, comm_fd);
        continue;
      }
      else if (input.substr(0, 4) == "quit")
      {
        response = "+OK Goodbye!\r\n";
        verbose_check(comm_fd, response, verbose);
        sendResponse(response, comm_fd);

        // if (mailboxData->deletedEmails.size() > 0)
        // {
        writeNewMailBox(mailboxData, emailSession, directory);
        // }

        pthread_mutex_lock(&connectionMutex);
        active_connections--;
        pthread_mutex_unlock(&connectionMutex);
        pthread_mutex_unlock(rcptMutexes[emailSession->username]);

        if (flock(fd, LOCK_UN) == -1)
        {
          std::cerr << "Error: Unable to unlock the file." << std::endl;
        }

        close(fd);
        emailSession->done = true;
        break;
      }
      else
      {
        response = "-ERR Unknown command\r\n";
        verbose_check(comm_fd, response, verbose);
        sendResponse(response, comm_fd);
      }
    }
    if (emailSession->done)
    {
      break;
    }
    // cout << "Server running: " << server_running << endl;
  }
  close(comm_fd);
  pthread_exit(NULL);
}

void sizeAndCountCalculate(std::shared_ptr<MailboxData> &mailboxData, ssize_t &totalSize, ssize_t &totalEmails)
{
  for (int i = 0; i < mailboxData->mailSizes.size(); i++)
  {
    if (mailboxData->deletedEmails.find(to_string(stoi(mailboxData->mailIds[i]) - 1)) == mailboxData->deletedEmails.end())
    {
      totalSize += mailboxData->mailSizes[i];
      totalEmails++;
    }
  }
}

void totalSizeCalculate(std::shared_ptr<MailboxData> &mailboxData, ssize_t &total)
{
  for (int i = 0; i < mailboxData->numEmails; i++)
  {
    if (mailboxData->deletedEmails.find(to_string(stoi(mailboxData->mailIds[i]) - 1)) == mailboxData->deletedEmails.end())
    {
      total += mailboxData->mailSizes[i];
    }
  }
}
int main(int argc, char *argv[])
{
  /* Your code here */
  signal(SIGINT, handle_sigint);
  int sockfd = socket(PF_INET, SOCK_STREAM, 0);

  // cout << "Socket created" << sockfd << endl;
  if (sockfd < 0)
  {
    fprintf(stderr, "Error creating socket\n");
  }

  int server_port = 11000;
  for (int i = 1; i < argc; i++)
  {
    // cout << argv[i] << endl;
    if (strcmp(argv[i], "-p") == 0)
    {
      if (i + 1 < argc)
      {
        int port = atoi(argv[i + 1]);
        if (port > 0)
        {
          server_port = port;
        }
      }
    }
    if (strcmp(argv[i], "-a") == 0)
    {
      fprintf(stderr, "Author: Mikhael Zacharias Thomas / mikth\n");
      close(sockfd);
      return 0;
    }
    if (strcmp(argv[i], "-v") == 0)
    {
      verbose = 1;
    }
    if (i + 1 == argc)
    {
      directory = argv[i];
    }
  }

  extractRecipients(directory, actual_recipients);

  for (int i = 0; i < actual_recipients.size(); i++)
  {
    string email = actual_recipients[i];
    pthread_mutex_t *mutex = new pthread_mutex_t;
    pthread_mutex_init(mutex, NULL);
    rcptMutexes[email] = mutex;
    std::cout << "Email is " << email << std::endl;
  }

  struct sockaddr_in server_addr;
  bzero(&server_addr, sizeof(server_addr));
  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(server_port);

  int opt = 1;
  int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt));

  if (bind(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
  {
    fprintf(stderr, "Error binding socket\n");
    close(sockfd);
    return 1;
  }

  listen(sockfd, 5);

  while (true)
  {
    struct sockaddr_in client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    if (server_running == false)
    {
      break;
    }

    int *comm_fd = (int *)malloc(sizeof(int));
    *comm_fd = accept(sockfd, (struct sockaddr *)&client_addr, &client_addr_len);

    if (*comm_fd < 0)
    {
      fprintf(stderr, "Error accepting connection\n");
      free(comm_fd);
      continue;
    }
    connections.push_back(*comm_fd);
    // dont recieve any connection, if server_running =false

    // fprintf(stdout, "+OK Server ready (Author: %s)\n", "Mikhael Zacharias Thomas / mikth");
    if (verbose)
    {
      fprintf(stdout, "[%d] New Connection\n", *comm_fd);
    }

    pthread_mutex_lock(&connectionMutex);
    if (active_connections < MAX_CONNECTIONS)
    {
      active_connections++;
      pthread_mutex_unlock(&connectionMutex);

      pthread_t thread;
      pthread_create(&thread, NULL, worker, comm_fd);
      threads.push_back(thread);
      // store comm_fd and then delete it after getting quit or shutdown

      pthread_detach(thread);
    }
    else
    {
      pthread_mutex_unlock(&connectionMutex);
      fprintf(stderr, "Max connections reached\n");
      close(*comm_fd);
      free(comm_fd);
    }

    // printf("Connection from %s\n", inet_ntoa(client_addr.sin_addr));
    // pthread_t thread;
    // pthread_create(&thread, NULL, worker, comm_fd);
  }

  close(sockfd);

  return 0;
}