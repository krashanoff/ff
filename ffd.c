#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>

#include <dirent.h>

#include <errno.h>
#include <err.h>
#include <string.h>

#include <poll.h>

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "proto.h"

int fd = -1;
char recv_buf[1000 * 1000] = {0}; // 1M is more than enough
char send_buf[1000 * 1000] = {0};

int main(int argc, char **argv)
{
  if (argc > 2)
  {
    if (chdir(argv[2]) < 0)
    {
      fprintf(stderr, "failed to chdir\n");
      exit(EXIT_FAILURE);
    }
  }

  if ((fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    exit(EXIT_FAILURE);
  }

  struct sockaddr_in saddr = {0};
  saddr.sin_family = AF_INET;
  uint16_t port_number = 8080;
  char *port_envvar = getenv("FFD_PORT");
  if (port_envvar != NULL)
    port_number = atoi(port_envvar);
  if (port_number < 0)
  {
    exit(EXIT_FAILURE);
  }
  saddr.sin_port = htons(port_number);
  if (inet_aton(argv[1], &saddr.sin_addr) < 0)
  {
    fprintf(stderr, "invalid address\n");
    exit(EXIT_FAILURE);
  }

  if (bind(fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0)
    exit(EXIT_FAILURE);

  struct sockaddr_in incoming = {0};
  socklen_t incoming_size = sizeof(incoming);
  struct pollfd fds[] = {
      {fd, POLLIN | POLLHUP, 0},
  };
  int rc = 0;
  while ((rc = poll(fds, 1, -1)) > 0)
  {
    if (fds[0].revents & POLLHUP)
    {
      fprintf(stderr, "disconnected\n");
      exit(EXIT_FAILURE);
    }
    if (fds[0].revents & POLLIN)
    {
      while ((rc = recvfrom(fd, &recv_buf, sizeof(recv_buf), 0, &incoming, &incoming_size)) > 0)
      {
        uint16_t packet_len = (((uint8_t)recv_buf[0]) << 8) | (((uint8_t)recv_buf[1]));
        uint8_t utag = (uint8_t)recv_buf[2];
        uint8_t request_type = ((utag & 0xF0) >> 4) & 0x0F;
        uint8_t tag = utag & 0x0F;
        uint16_t requested_buffer_size = ((uint8_t)recv_buf[3] << 8) | ((uint8_t)recv_buf[4]);

        // if ((packet_len != rc) || (packet_len >= BUFSIZ))
        // {
        //   memset(recv_buf, 0, BUFSIZ);
        //   // Set packet size
        //   recv_buf[0] = 1;
        //   recv_buf[1] = 1;
        //   // Set packet intent
        //   recv_buf[2] |= (((uint8_t)RESPONSE_ERR) << 4);
        //   memcpy(recv_buf + 3, "bad packet len", 15 * sizeof(char));
        //   if (sendto(fd, recv_buf, 18 * sizeof(char), 0, &incoming, incoming_size) < 0)
        //     exit(EXIT_FAILURE);
        //   continue;
        // }

        // Pull out the packet data portion.
        char *data = recv_buf + 4;
        data[packet_len] = '\0';

        fprintf(stderr, "received packet with len %x, type %d, tag %d, data %s\n", packet_len, request_type, tag, data);

        if (request_type == REQUEST_LS)
        {
          fprintf(stderr, "ls files\n");

          // If just a request with no args, then open the cwd
          DIR *dirp = NULL;
          if (packet_len == 3)
          {
            dirp = opendir(".");
          }
          else
          {
            dirp = opendir("data");
          }
          if (dirp == NULL)
          {
            fprintf(stderr, "error %s\n", strerror(errno));
          }

          struct dirent *entry = NULL;
          while ((entry = readdir(dirp)) != NULL)
          {
            if (entry->d_type == DT_DIR)
            {
              fprintf(stderr, "directory\n");
            }
          }
          if (errno != 0)
          {
            fprintf(stderr, "error %s\n", strerror(errno));
          }
          closedir(dirp);
        }

        // TODO: if requesting the entire file, then fork another process to handle it
        // rc = sendto(fd, "HI", 3 * sizeof(char), 0, &incoming, incoming_size);
        // if (rc < 0)
        // {
        //   fprintf(stderr, "failed to send %s\n", strerror(errno));
        // }

        // if (recv_buf[0] != 'R')
        // {
        //   sendto(fd, "Erequest", 9 * sizeof(char), 0, &incoming, incoming_size);
        //   continue;
        // }

        // TODO: check if requesting specific position in file

        // *(recv_buf + rc) = 0;
        // int file = open(recv_buf + 1, O_RDONLY);
        // if (file < 0)
        // {
        //   sendto(fd, "Eopen", 6 * sizeof(char), 0, &incoming, &incoming_size);
        //   continue;
        // }

        // int current_position = 0;
        // while ((rc = read(file, recv_buf, sizeof(recv_buf))) > 0)
        // {
        //   int size = snprintf(send_buf, sizeof(send_buf), "%d\\%d%.*s", current_position, rc, rc, recv_buf);
        //   sendto(fd, send_buf, size, 0, &incoming, &incoming_size);
        // }
        // close(file);
      }

      if (rc < 0)
      {
        fprintf(stderr, "%s", strerror(errno));
        close(fd);
        exit(EXIT_FAILURE);
      }
    }
  }

  close(fd);
  if (rc < 0)
  {
    fprintf(stderr, "%s", strerror(errno));
    exit(EXIT_FAILURE);
  }
  return 0;
}