#include <stdlib.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>
#include "utils.h"
#include <signal.h>
#include <vector>
#include <algorithm>
#include "smtp.h"
#include <sys/stat.h>
#include <unordered_map>
#include "filemanager.h"
#include <memory>
#include "email_session.h"

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
// create directory first and map mutexes

void verbose_check(int comm_fd, string buffer)
{
  if (verbose)
  {
    fprintf(stdout, "[%d] S: %s", comm_fd, buffer.c_str());
  }
}

void handle_sigint(int sig)
{
  // cout << "Caught signal: " << sig << endl;

  string shutting_down = "-ERR Server shutting down\r\n";
  server_running = false;
  verbose_check(0, shutting_down);
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
  shared_ptr<EmailSession> emailSession = shared_ptr<EmailSession>(new EmailSession(*(int *)arg));
  string greeting = "220 localhost service ready\r\n";
  if (verbose)
  {
    fprintf(stdout, "[%d] S: %s", emailSession->comm_fd, greeting.c_str());
  }
  // unsigned short wlen = htons(greeting.size());
  // do_write(comm_fd, (char *)&wlen, sizeof(wlen));
  do_write(emailSession->comm_fd, const_cast<char *>(greeting.c_str()), greeting.size());
  // cout << "Worker started" << endl;
  string buffer;

  while (true)
  {
    // cout << "Worker loop" << endl;
    // unsigned short rlen;

    if (emailSession->data_incoming)
    {
      cout << "Data incoming" << endl;
      receiving_mail(emailSession, directory, rcptMutexes, verbose);

      if (emailSession->done)
      {
        break;
      }

      continue;
    }

    char buf[1024];
    ssize_t bytes_received = recv(emailSession->comm_fd, buf, sizeof(buf) - 1, 0);
    if (bytes_received <= 0)
    {
      break;
    }

    // buf[bytes_received] = '\0';
    buffer.append(buf, bytes_received);

    size_t pos;

    while ((pos = buffer.find("\r\n")) != string::npos)
    {
      if (verbose)
      {
        fprintf(stdout, "[%d] C: %s \n", emailSession->comm_fd, buffer.substr(0, pos).c_str());
      }
      string input = buffer.substr(0, pos);
      buffer.erase(0, pos + 2);
      // string response;

      std::transform(input.begin(), input.end(), input.begin(), ::tolower);

      string response;
      // for (int i = 0; i < buffer.size(); i++)
      // {
      //   printf("%c", buffer[i]);
      // }
      if (input.substr(0, 5) == "helo ")
      {

        // check whether the argument is not null
        if (input.size() == 5)
        {
          response = "501 Syntax error\r\n";
          verbose_check(emailSession->comm_fd, response);
          sendResponse(response, emailSession->comm_fd);
          continue;
        }

        string text = input.substr(5);
        response = "250 " + text + "\r\n";
        verbose_check(emailSession->comm_fd, response);
        sendResponse(response, emailSession->comm_fd);
        emailSession->hello_got = true;
        continue;
      }
      else if (input.substr(0, 10) == "mail from:")
      {
        if (emailSession->hello_got == false)
        {
          helloCheck(response, emailSession->comm_fd);
          continue;
        }
        else
        {
          string text = input.substr(10);
          response = "250 OK\r\n";
          verbose_check(emailSession->comm_fd, response);
          sendResponse(response, emailSession->comm_fd);
          emailSession->mail_from_got = true;
          emailSession->mail_from = text;
          continue;
        }
      }
      else if (input.substr(0, 8) == "rcpt to:")
      {
        if (emailSession->mail_from_got == false)
        {
          helloCheck(response, emailSession->comm_fd);
          continue;
        }
        string email_address = input.substr(8);
        // cout << "Email address is" << email_address << endl;
        bool validation = isValidRecipient(email_address, directory, actual_recipients, "SMTP");

        if (!validation)
        {
          response = "550 Invalid Recipient\r\n";
          verbose_check(emailSession->comm_fd, response);
          sendResponse(response, emailSession->comm_fd);
          continue;
        }
        emailSession->is_recipient = true;

        emailSession->recipients.push_back(email_address.substr(email_address.find("<") + 1, email_address.find("@") - (email_address.find("<") + 1)));
        response = "250 OK\r\n";
        verbose_check(emailSession->comm_fd, response);
        sendResponse(response, emailSession->comm_fd);
        continue;
        // }else{
        //   string
        // }
      }
      else if (input.substr(0, 4) == "data")
      {
        if (emailSession->hello_got == false || emailSession->mail_from_got == false || emailSession->is_recipient == false)
        {
          response = "503 Error\r\n";
          verbose_check(emailSession->comm_fd, response);
          sendResponse(response, emailSession->comm_fd);
          continue;
        }
        emailSession->data_incoming = true;
        response = "354 Start mail input; end with <CRLF>.<CRLF>\r\n";
        verbose_check(emailSession->comm_fd, response);
        sendResponse(response, emailSession->comm_fd);
        continue;
      }
      else if (input.substr(0, 4) == "rset")
      {

        if (emailSession->hello_got == false)
        {
          response = "503 Error\r\n";
          verbose_check(emailSession->comm_fd, response);
          sendResponse(response, emailSession->comm_fd);
          continue;
        }

        // string text = input.substr(5);
        response = "+OK \r\n";
        // fprintf(stdout, "%s\n", text.c_str());
        // print contents in buffer
        verbose_check(emailSession->comm_fd, response);
        sendResponse(response, emailSession->comm_fd);
        emailSession->hello_got = false;
        emailSession->mail_from_got = false;
        emailSession->is_recipient = false;
        emailSession->recipients.clear();
        continue;
      }
      else if (input.substr(0, 4) == "quit")
      {
        response = "221 Bye\r\n";
        verbose_check(emailSession->comm_fd, response);
        sendResponse(response, emailSession->comm_fd);
        pthread_mutex_lock(&connectionMutex);
        active_connections--;
        pthread_mutex_unlock(&connectionMutex);
        emailSession->hello_got = false;
        emailSession->mail_from_got = false;
        emailSession->is_recipient = false;

        emailSession->done = true;
        break;
      }
      else if (input.substr(0, 4) == "noop")
      {
        if (emailSession->hello_got == false)
        {
          response = "503 Error\r\n";
          verbose_check(emailSession->comm_fd, response);
          sendResponse(response, emailSession->comm_fd);
          continue;
        }

        response = "+OK\r\n";
        verbose_check(emailSession->comm_fd, response);
        sendResponse(response, emailSession->comm_fd);
      }
      else if (input.substr(0, 4) == "ehlo")
      {
        response = "500 syntax error\r\n";
        verbose_check(emailSession->comm_fd, response);
        sendResponse(response, emailSession->comm_fd);
      }
      else
      {
        response = "-ERR Unknown command\r\n";
        verbose_check(emailSession->comm_fd, response);
        sendResponse(response, emailSession->comm_fd);
      }
    }
    if (emailSession->done)
    {
      break;
    }
    // cout << "Server running: " << server_running << endl;
  }
  // shutdown_client(comm_fd);
  close(emailSession->comm_fd);
  pthread_exit(NULL);
}

void helloCheck(std::string &response, int comm_fd)
{
  response = "503 Error";
  verbose_check(comm_fd, response);
  sendResponse(response, comm_fd);
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

  int server_port = 10000;
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
    // std::cout << "Email is " << email << std::endl;
  }
  // std::cout << "rcptMutexes size is " << rcptMutexes.size() << std::endl;

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
  for (auto &pair : rcptMutexes)
  {
    pthread_mutex_destroy(pair.second);
    delete pair.second;
  }

  return 0;
}
