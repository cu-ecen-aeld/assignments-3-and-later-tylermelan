#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stddef.h>
#include <unistd.h>
#include <stdio.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <signal.h>
#include <string.h>

#define BUF_SIZE 1024
#define FILE_PATH "/var/tmp/aesdsocketdata"

bool should_exit = false;

static void exit_action_handler(int signal_number)
{
  should_exit = true;
  syslog(LOG_DEBUG, "Caught signal, exiting");
}

static int setup_signals()
{
  int status;
  struct sigaction exit_action = {0};
  exit_action.sa_handler = exit_action_handler;

  status = sigaction(SIGINT, &exit_action, NULL);
  if (status != 0)
  {
    perror("sigaction SIGINT");
    return -1;
  }

  status = sigaction(SIGTERM, &exit_action, NULL);
  if (status != 0)
  {
    perror("sigaction SIGTERM");
    return -1;
  }

  return 0;
}

static int listen_on_socket()
{
  int status;
  struct addrinfo *servinfo;
  struct addrinfo hints = {0};
  hints.ai_flags = AI_PASSIVE;
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  status = getaddrinfo(NULL, "9000", &hints, &servinfo);
  if (status != 0)
  {
    fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
    return -1;
  }

  int sockfd = socket(servinfo->ai_family, servinfo->ai_socktype, servinfo->ai_protocol);
  if (sockfd == -1)
  {
    perror("socket error");
    return -1;
  }

  int opt = 1;
  status = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  if (status != 0)
  {
    perror("setsockopt error");
    return -1;
  }

  status = bind(sockfd, servinfo->ai_addr, sizeof(struct sockaddr));
  if (status != 0)
  {
    perror("bind error");
    return -1;
  }

  status = listen(sockfd, 10);
  if (status != 0)
  {
    perror("listen error");
    return -1;
  }

  freeaddrinfo(servinfo);
  return sockfd;
}

static int accept_connection(int sockfd, char *client_ip)
{
  struct sockaddr addr;
  socklen_t addr_size = sizeof(addr);

  int acceptfd = accept(sockfd, &addr, &addr_size);
  if (acceptfd == -1)
  {
    perror("accept error");
    return -1;
  }

  struct sockaddr_in *addr_in = (struct sockaddr_in *)&addr;

  if (inet_ntop(AF_INET, &addr_in->sin_addr, client_ip, INET_ADDRSTRLEN) == NULL)
  {
    perror("inet_ntop error");
    return -1;
  }

  syslog(LOG_DEBUG, "Accepted connection from %s", client_ip);

  return acceptfd;
}

static int recv_packet(int acceptfd)
{
  FILE *fp = fopen(FILE_PATH, "a");
  if (fp == NULL)
  {
    perror("fopen error");
    return -1;
  }

  char buf[BUF_SIZE];
  bool packet_complete = false;
  while (!packet_complete)
  {
    ssize_t nread = recv(acceptfd, buf, BUF_SIZE, 0);
    if (nread == -1)
    {
      perror("recv error");
      break;
    }

    size_t i = 0;
    for (; i < nread; ++i)
    {
      if (buf[i] == '\n')
      {
        packet_complete = true;
        break;
      }
    }

    if (i != nread)
    {
      i++;
    }

    fwrite(buf, sizeof(char), i, fp);
  }

  fclose(fp);

  return packet_complete ? 0 : -1;
}

static int send_updated_file(int acceptfd)
{
  int file_fd = open(FILE_PATH, O_RDONLY);
  if (file_fd < 0)
  {
    perror("open");
    return -1;
  }

  struct stat st;
  if (fstat(file_fd, &st) < 0)
  {
    perror("fstat");
    close(file_fd);
    return -1;
  }

  off_t offset = 0;
  ssize_t remaining = st.st_size;
  while (remaining > 0)
  {
    ssize_t sent = sendfile(acceptfd, file_fd, &offset, remaining);
    if (sent == -1)
    {
      perror("sendfile");
      break;
    }
    remaining -= sent;
  }

  close(file_fd);
  return remaining > 0 ? -1 : 0;
}

int main(int argc, char *argv[])
{
  int status;
  openlog(NULL, 0, LOG_USER);

  status = setup_signals();
  if (status != 0)
  {
    return -1;
  }

  int sockfd = listen_on_socket();
  if (sockfd == -1)
  {
    return -1;
  }

  if (argc > 1 && strcmp(argv[1], "-d") == 0)
  {
    pid_t pid = fork();
    if (pid < 0)
    {
      perror("fork");
      return -1;
    }

    if (pid != 0)
    {
      close(sockfd);
      return 0;
    }

    setsid();
    chdir("/");
  }

  while (!should_exit)
  {

    char client_ip[INET_ADDRSTRLEN];
    int acceptfd = accept_connection(sockfd, client_ip);
    if (acceptfd == -1)
    {
      continue;
    }

    status = recv_packet(acceptfd);
    if (status != 0)
    {
      close(acceptfd);
      continue;
    }

    send_updated_file(acceptfd);

    close(acceptfd);
    syslog(LOG_DEBUG, "Closed connection from %s", client_ip);
  }

  close(sockfd);
  remove(FILE_PATH);

  return 0;
}
