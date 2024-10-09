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

using namespace std;

const int MAX_CONNECTIONS = 100;
int active_connections = 0;
pthread_mutex_t connectionMutex = PTHREAD_MUTEX_INITIALIZER;
int verbose = 0;
bool server_running = true;
vector<int> connections;
vector<pthread_t> threads;
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
  bool done = false;
  string greeting = "+OK Server ready (Author: Mikhael Zacharias Thomas / mikth)\r\n";
  if (verbose)
  {
    fprintf(stdout, "[%d] S: %s", comm_fd, greeting.c_str());
  }
  // unsigned short wlen = htons(greeting.size());
  // do_write(comm_fd, (char *)&wlen, sizeof(wlen));
  do_write(comm_fd, const_cast<char *>(greeting.c_str()), greeting.size());
  // cout << "Worker started" << endl;
  string buffer;
  while (true)
  {
    // cout << "Worker loop" << endl;
    // unsigned short rlen;
    char buf[1024];
    ssize_t bytes_received = recv(comm_fd, buf, sizeof(buf) - 1, 0);
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
        fprintf(stdout, "[%d] C: %s \n", comm_fd, buffer.substr(0, pos).c_str());
      }
      string input = buffer.substr(0, pos);
      buffer.erase(0, pos + 2);
      string response;
      string inputPrefix = input.substr(0, 5);

      // Convert inputPrefix to lowercase
      std::transform(inputPrefix.begin(), inputPrefix.end(), inputPrefix.begin(), ::tolower);

      // for (int i = 0; i < buffer.size(); i++)
      // {
      //   printf("%c", buffer[i]);
      // }
      // handle case insensitive
      if (inputPrefix == "echo ")
      {
        string text = input.substr(5);
        response = "+OK " + text + "\r\n";
        // fprintf(stdout, "%s\n", text.c_str());
        // print contents in buffer
        verbose_check(comm_fd, response, verbose);
        sendResponse(response, comm_fd);
      }
      else if (inputPrefix == "quit")
      {
        response = "+OK Goodbye!\r\n";
        verbose_check(comm_fd, response, verbose);
        sendResponse(response, comm_fd);
        pthread_mutex_lock(&connectionMutex);
        active_connections--;
        pthread_mutex_unlock(&connectionMutex);
        done = true;
        break;
      }
      else
      {
        response = "-ERR Unknown command\r\n";
        verbose_check(comm_fd, response, verbose);
        sendResponse(response, comm_fd);
      }
    }
    if (done)
    {
      break;
    }
    // cout << "Server running: " << server_running << endl;
  }
  close(comm_fd);
  pthread_exit(NULL);
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
