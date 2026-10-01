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

int percorrer(char *dirname)
{
  DIR *dirp;
  struct dirent *dp;
  dirp = opendir(dirname);
  if (dirname == NULL)
  {
    return -1;
  }

  if (dirp == NULL)
  {
    perror("opendir failed");
    percorrer(NULL);
  }

  for (;;)
  {
    dp = readdir(dirp);
    if (dp == NULL && filter_dots(dp))
    {
      break;
    }

    // atualiza o path depois de descer uma diretoria
    char newpath[257];
    snprintf(newpath, 257, "%s/%s", dirname, dp->d_name);

    // opendir -> testa se é uma diretoria ou ficheiro
    if (opendir(newpath) == NULL && filter_dots(dp))
    {
      // copiar
    }

    else if (opendir(newpath) && filter_dots(dp))
    {
      percorrer(newpath);
    };
  }
  return 0;
}

int transferir_inputs(char *raiz)
{
  if (!percorrer(raiz))
  {
    perror("Ocorreu um erro na cópia dos ficheiros");
    return -1;
  }
  return 0;
}