#pragma once
void helloCheck(std::string &response, int comm_fd);

void receiving_mail(int comm_fd, std::vector<std::string> &data, bool &data_incoming, std::vector<std::string> &recipients, std::string &mail_from, bool &mail_from_got, bool &is_recipient, bool &helo_got, bool &done);

int processing_arguments(int argc, char *argv[], int &server_port, int sockfd, bool &retFlag);
