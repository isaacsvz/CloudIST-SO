#define _XOPEN_SOURCE 700
#define BUFSIZE 64
#define RESERVATIONS_PATHNAME "/tmp/CloudIST"
#define MAX_PATH_SIZE 1024

#include "filesystem.h"

#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int path_exists(const char *path) {
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISDIR(st.st_mode);
}

int file_exists(const char *path) {
    struct stat st;

    if (stat(path, &st) != 0)
        return 0;

    return S_ISREG(st.st_mode);
}

int absolute_path(const char *path, char *buffer, size_t size) {
    char *resolved = realpath(path, NULL);

    if (resolved == NULL)
        return 1;

    if (strlen(resolved) >= size) {
        free(resolved);
        return 1;
    }

    strcpy(buffer, resolved);

    free(resolved);
    return 0;
}

int my_open(char *path) { return open(path, O_RDONLY); }

int filter_dots(const struct dirent *entry) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
        return 0; // Excluir da lista
    }
    return 1; // Incluir na lista
}

int copiar_ficheiros(const char *origem, int out_fd) {
    int in_fd = open(origem, O_RDONLY);
    if (in_fd < 0)
        return -1; // retorna erro se nao conseguir abrir ficheiro original

    if (out_fd < 0)
        return -1;

    char buf[64];
    ssize_t bytes_lidos;
    ssize_t bytes_escritos;

    while (1) {
        bytes_lidos = read(in_fd, buf, sizeof(buf));

        if (bytes_lidos == 0) { // chegamos ao fim do ficheiro
            break;
        }

        if (bytes_lidos < 0) { // ocorre erro
            close(in_fd);
            close(out_fd);
            return -1;
        }

        bytes_escritos = write(out_fd, buf, (size_t)bytes_lidos);

        if (bytes_escritos != bytes_lidos) {
            close(in_fd);
            close(out_fd);
            return -1;
        }
    }

    close(in_fd);
    close(out_fd);
    return 0;
}

int new_dir_reserve(char *reservation_id, char *dest_dir) {
    snprintf(dest_dir, MAX_PATH_SIZE, "%s/%s", RESERVATIONS_PATHNAME,
             reservation_id);
    if (mkdir(dest_dir, 0777) < 0) {
        perror("Erro ao criar diretoria da reserva");
        return -1;
    };
    return 0;
}

int new_dir_vm(char *vm_id, char *res_path, char *dest_dir) {
    snprintf(dest_dir, MAX_PATH_SIZE, "%s/%s", res_path, vm_id);
    if (mkdir(dest_dir, 0777) < 0) {
        perror("Erro ao criar diretoria da VM");
        return -1;
    };
    return 0;
}

int traverse_dir(char *dirname_in, char *dirname_res) {
    DIR *dirp_in;
    struct dirent *dp_in;
    dirp_in = opendir(dirname_in);
    if (dirname_in == NULL) {
        return -1;
    }

    if (dirp_in == NULL) {
        perror("opendir failed");
        traverse_dir(NULL, NULL);
    }

    DIR *dirp_res;
    dirp_res = opendir(dirname_res);
    if (dirname_res == NULL) {
        return -1;
    }

    if (dirp_res == NULL) {
        perror("opendir failed");
        traverse_dir(NULL, NULL);
    }

    for (;;) {
        // dp_in -> próxima entrada do diretorio de input
        dp_in = readdir(dirp_in);
        if (dp_in == NULL) {
            break;
        }

        // atualiza o pathname na dir dos inputs depois de descer uma diretoria
        char newpath_in[257];
        snprintf(newpath_in, 257, "%s/%s", dirname_in, dp_in->d_name);

        // atualizar também o pathname na dir da reserva, que poderá ser o path
        // de um ficheiro ou de uma dir
        char newpath_res[257];
        snprintf(newpath_res, 257, "%s/%s", dirname_res, dp_in->d_name);

        // opendir -> testa se é uma diretoria ou ficheiro
        if (opendir(newpath_in) == NULL && filter_dots(dp_in)) {
            // é um ficheir ->copiar para a dir da vm (fica com o mesmo nome)
            int fd = open(newpath_res, O_CREAT | O_RDWR, 0666);
            if (copiar_ficheiros(newpath_in, fd) == -1) {
                return -1;
            }
        }

        else if (opendir(newpath_in) && filter_dots(dp_in)) {
            // cria a diretoria nova na reserva
            mkdir(newpath_res, 0777);
            traverse_dir(newpath_in,
                         newpath_res); // é uma diretoria -> descer na dir dos
                                       // inputs e na reserva
        };
    }
    return 0;
}

int transferir_inputs(char *raiz_inputs, char *raiz_reserva) {
    if (traverse_dir(raiz_inputs, raiz_reserva) == -1) {
        perror("Erro na cópia dos ficheiros");
        return -1;
    }
    return 0;
}