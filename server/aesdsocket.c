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
#include <stdlib.h>
#include <pthread.h>
#include "queue.h"

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

static int recv_packet(int acceptfd, pthread_mutex_t *mutex)
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
    pthread_mutex_lock(mutex);
    fwrite(buf, sizeof(char), i, fp);
    pthread_mutex_unlock(mutex);
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

typedef struct slist_data_s slist_data_t;
struct slist_data_s
{
  pthread_t thread;
  struct connection_data *connection_data;
  SLIST_ENTRY(slist_data_s)
  entries;
};

struct connection_data
{
  int acceptfd;
  char *client_ip;
  bool complete;
  pthread_mutex_t *mutex;
};

void *handle_connection(void *thread_param)
{
  struct connection_data *thread_func_args = (struct connection_data *)thread_param;
  int acceptfd = thread_func_args->acceptfd;
  char *client_ip = thread_func_args->client_ip;
  pthread_mutex_t *mutex = thread_func_args->mutex;

  int status = recv_packet(acceptfd, mutex);
  if (status != 0)
  {
    close(acceptfd);
    thread_func_args->complete = true;
    return NULL;
  }

  send_updated_file(acceptfd);

  close(acceptfd);
  syslog(LOG_DEBUG, "Closed connection from %s", client_ip);

  thread_func_args->complete = true;
  return NULL;
}

static void handle_timer(union sigval sigval)
{
  pthread_mutex_t *mutex = (pthread_mutex_t *)sigval.sival_ptr;

  FILE *fp = fopen(FILE_PATH, "a");
  if (fp == NULL)
  {
    perror("fopen");
    return;
  }

  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  char buf[64];
  strftime(buf, sizeof(buf), "%Y-%m-%d_%H:%M:%S", tm_info);

  pthread_mutex_lock(mutex);
  fprintf(fp, "timestamp:%s\n", buf);
  pthread_mutex_unlock(mutex);

  fclose(fp);
  return;
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

  pthread_mutex_t mutex;
  pthread_mutex_init(&mutex, NULL);

  timer_t timerid;
  struct sigevent sev;
  struct itimerspec its;

  memset(&sev, 0, sizeof(sev));
  sev.sigev_notify = SIGEV_THREAD;
  sev.sigev_value.sival_ptr = &mutex;
  sev.sigev_notify_function = handle_timer;
  timer_create(CLOCK_REALTIME, &sev, &timerid);

  its.it_value.tv_sec = 0;
  its.it_value.tv_nsec = 1;
  its.it_interval.tv_sec = 10;
  its.it_interval.tv_nsec = 0;
  timer_settime(timerid, 0, &its, NULL);

  SLIST_HEAD(slisthead, slist_data_s)
  head;
  SLIST_INIT(&head);
  slist_data_t *datap = NULL;
  slist_data_t *tdatap = NULL;

  while (!should_exit)
  {
    char *client_ip = malloc(INET_ADDRSTRLEN);
    int acceptfd = accept_connection(sockfd, client_ip);
    if (acceptfd == -1)
    {
      free(client_ip);
      continue;
    }

    pthread_t thread;
    struct connection_data *connection_data = malloc(sizeof(struct connection_data));
    connection_data->acceptfd = acceptfd;
    connection_data->client_ip = client_ip;
    connection_data->complete = false;
    connection_data->mutex = &mutex;

    status = pthread_create(&thread, NULL, handle_connection, connection_data);

    datap = malloc(sizeof(slist_data_t));
    datap->thread = thread;
    datap->connection_data = connection_data;
    SLIST_INSERT_HEAD(&head, datap, entries);

    SLIST_FOREACH_SAFE(datap, &head, entries, tdatap)
    {
      if (datap->connection_data->complete)
      {
        SLIST_REMOVE(&head, datap, slist_data_s, entries);
        pthread_join(datap->thread, NULL);
        free(datap->connection_data->client_ip);
        free(datap->connection_data);
        free(datap);
      }
    }
  }

  while (!SLIST_EMPTY(&head))
  {
    datap = SLIST_FIRST(&head);
    SLIST_REMOVE_HEAD(&head, entries);
    pthread_join(datap->thread, NULL);
    free(datap->connection_data->client_ip);
    free(datap->connection_data);
    free(datap);
  }

  timer_delete(timerid);
  close(sockfd);
  remove(FILE_PATH);
  pthread_mutex_destroy(&mutex);

  return 0;
}
