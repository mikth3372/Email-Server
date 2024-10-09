#ifndef UTILS_H
#define UTILS_H

#include <iostream>
#include <string>
#include <vector>

bool do_read(int fd, char *buf, int len);
bool do_write(int fd, char *buf, int len);
void sendResponse(std::string &response, int comm_fd);
bool validateEmail(std::string email);
bool fileExists(const std::string &name);
bool isValidRecipient(const std::string &email, const std::string &directory, std::vector<std::string> &recipients, std::string usecase);
void shutdown_client(int comm_fd);
void verbose_check(int comm_fd, std::string buffer, bool verbose);

#endif