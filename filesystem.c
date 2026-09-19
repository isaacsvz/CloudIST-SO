#define _XOPEN_SOURCE 700
#define BUFSIZE 64

#include "filesystem.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>

int path_exists(const char *path)
{
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISDIR(st.st_mode);
}

int file_exists(const char *path)
{
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISREG(st.st_mode);
}

int absolute_path(const char *path, char *buffer, size_t size)
{
  char *resolved = realpath(path, NULL);

  if (resolved == NULL)
    return 1;

  if (strlen(resolved) >= size)
  {
    free(resolved);
    return 1;
  }

  strcpy(buffer, resolved);

  free(resolved);
  return 0;
}

int confToStdin(char *path)
{

  char file_path[256];
  snprintf(file_path, sizeof(file_path), "%s1.conf", path);
  // read from conf
  int fd = open(file_path, O_RDONLY);
  if (fd < 0)
  {
    perror("Failed opening conf file");
    return 0;
  }

  while (1)
  {
    // Always clean the buffer
    char buffer[BUFSIZE + 1] = {0};
    // read from conf to buffer
    ssize_t bytes_read = 0;
    size_t done = 0;
    size_t len = BUFSIZE;
    while (BUFSIZE > done)
    {

      bytes_read = read(fd, buffer + done, (size_t)(len - done));
      if (bytes_read < 0)
      {
        perror("Failed processing conf file");
        return 0;
      }
      if (bytes_read < BUFSIZE)
      {
        buffer[bytes_read] = '\0';
      }

      // write from buffer to stdin
      ssize_t bytes_written = 0;
      done = 0;
      while (bytes_read >= 0 && (size_t)bytes_read > done)
      {

        bytes_written = write(1, buffer + done, (size_t)(len - done));
        if (bytes_read < 0)
        {
          perror("Failed processing conf file");
          return 0;
        }

        done += (size_t)bytes_written;
      }

      if (bytes_read == 0)
      {
        close(fd);
        return 0;
      }

      done += (size_t)bytes_read;
    }
  }

  close(fd);
  return 1;
}