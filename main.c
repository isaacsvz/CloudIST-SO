#include "constants.h"
#include "datacenter.h"
#include "parser.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
    DataCenter dc;
    datacenter_init(&dc);

    if (argc != 6) {
        fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <input_dir>\n",
                argv[0]);
        return 1;
    }

    size_t servers;
    size_t ram;
    size_t disk;
    double cpu;

    if (parse_size_t_arg(argv[1], &servers) != 0 ||
        parse_size_t_arg(argv[2], &ram) != 0 ||
        parse_size_t_arg(argv[3], &disk) != 0 ||
        parse_double_arg(argv[4], &cpu) !=
            0) //(NEW)verifica se foi introduzido o input dir
    {
        fprintf(stderr, "Invalid command line arguments.\n");
        return 1;
    }

    if (!path_exists(argv[5])) {
        fprintf(stderr, "Invalid input directory.\n");
        return 1;
    }

    Resources resources = {.ram = ram, .disk = disk, .cpu = cpu};

    if (datacenter_configure(&dc, servers, &resources) != 0) {
        fprintf(stderr, "Failed to configure Data Center.\n");
        return 1;
    }

    // Cria array de ponteiros para structs dirent
    struct dirent **file_name_list;
    int n = scandir(argv[5], &file_name_list, filter_dots, alphasort);

    if (n < 0) {
        perror("Failed to scan input directory");
        return EXIT_FAILURE;
    }

    for (int i = 0; i < n; i++) {

        // Concatenação da dir com o ficheiro para criar o file_path para o open
        // 256 + 1 -> por causa da barra
        char file_path[257];
        snprintf(file_path, sizeof(file_path), "%s/%s", argv[5],
                 file_name_list[i]->d_name);
        int fd = my_open(file_path);
        int runningfile = 1;
        while (runningfile) {
            switch (get_next_command(fd)) {
            case CMD_DEFINE: {
                VMType vmtype;

                if (parse_define(fd, &vmtype) != 0) {
                    fprintf(
                        stderr,
                        "Invalid define command. See H (help) for usage.\n");
                    continue;
                }

                if (datacenter_define_VM(&dc, &vmtype) != 0) {
                    fprintf(stderr, "Failed to define VM.\n");
                    continue;
                }

                printf("VM successfully defined!\n");

                break;
            }

            case CMD_RESERVE: {
                Reservation reservation = {0};

                size_t num_items =
                    parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);

                if (num_items == 0) {
                    fprintf(
                        stderr,
                        "Invalid reserve command. See H (help) for usage.\n");
                    continue;
                }

                if (datacenter_reserve(&dc, &reservation) != 0) {
                    fprintf(stderr, "Failed to reserve VMs.\n");
                    continue;
                }

                printf("Reservation made successfully!\n");

                // TODO - transferir_inputs aqui maybe

                // criar pasta da reserva
                char reserve_dir[MAX_PATH_SIZE];
                if (new_dir_reserve((&reservation)->id, reserve_dir) < 0) {
                    return -1;
                }

                for (size_t vm_index = 0; vm_index < reservation.num_vms;
                     vm_index++) {
                    VM *vm = reservation.vms[vm_index];
                    char vm_dir[MAX_PATH_SIZE];
                    if (new_dir_vm(vm->id, reserve_dir, vm_dir) < 0) {
                        return -1;
                    }
                    if (transferir_inputs(vm->type->input_folder, vm_dir)) {
                        return -1;
                    };
                }

                break;
            }

            case CMD_EXECUTE:
                char id[MAX_STRING_SIZE];

                if (parse_execute(fd, id) != 0) {
                    fprintf(
                        stderr,
                        "Invalid execute command. See H (help) for usage.\n");
                    continue;
                }

                if (datacenter_execute(&dc, id) != 0) {
                    fprintf(stderr, "Failed to execute reservation.\n");
                    continue;
                }

                printf("Finished reservation execution!\n");

                break;

            case CMD_LIST:
                if (datacenter_list(&dc) != 0) {
                    fprintf(stderr, "Failed to list VMs.\n");
                    continue;
                }

                break;

            case CMD_WAIT:
                unsigned int delay;

                if (parse_wait(fd, &delay) != 0) {
                    fprintf(stderr,
                            "Invalid wait command. See H (help) for usage.\n");
                    continue;
                }

                datacenter_wait(delay);
                break;

            case CMD_INVALID:
                fprintf(stderr, "Invalid Command. See H (help) for usage.\n");
                break;

            case CMD_HELP:
                printf(
                    "Spaces between arguments are allowed, but not after "
                    "command end.\n"
                    "Available commands:\n"
                    " D <VM_TYPE_ID> <INPUT_FOLDER> <EXECUTABLE_PATH> "
                    "<RAM_NEEDED> <DISK_NEEDED> <VCPU_NEEDED_COUNT>\n"
                    " R <RESERVATION_ID> [<VM_TYPE_ID> <COUNT> <SERVER_ID>]+\n"
                    " A <RESERVATION_ID>\n"
                    " L\n"
                    " E <DELAY_MS>\n"
                    " H\n");
                break;

            case CMD_EMPTY:
                break;

            case EOC:
                close(fd);
                runningfile = 0;
                break;
            }
        }
    }

    for (int i = 0; i < n; i++) {
        free(file_name_list[i]);
    }
    free(file_name_list);

    datacenter_destroy(&dc);
    return 0;
}