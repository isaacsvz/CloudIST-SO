#define _XOPEN_SOURCE 700
#define BUFSIZE 64

#include "filesystem.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <dirent.h>
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

int my_open(char *path)
{
  return open(path, O_RDONLY);
}

int filter_dots(const struct dirent *entry)
{
  if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
  {
    return 0; // Excluir da lista
  }
  return 1; // Incluir na lista
}