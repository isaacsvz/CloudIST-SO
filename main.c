#include "constants.h"
#include "datacenter.h"
#include "parser.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>



typedef struct {
    DataCenter *dc;
    double personal_cores;
    int fd;
} run_file_args;


void *waitGlobal(void *args){
    DataCenter *dc = (DataCenter*) args;
    int status;
    while(1){
        pid_t pid = wait(&status);
        if(pid < 0){
            datacenter_wait(10);
            continue;
        }

        VM *vm = NULL;
        pthread_rwlock_rdlock(&dc->rwlock);
        for(int i = 0; i < (int)dc->num_reservations && vm == NULL; i++){
            for(int j = 0; j < (int)dc->reservations[i].num_vms;j++){
                if(dc->reservations[i].vms[j]->pid == pid){
                    vm = dc->reservations[i].vms[j];
                    break;
                }
            }
        }
        pthread_rwlock_unlock(&dc->rwlock);

        if(vm == NULL) continue;
        
        pthread_rwlock_wrlock(&vm->server->rwlock);
        Resources finished_resources = vm->type->required; 
        resources_add(&vm->server->available, finished_resources);
        vm->state = VM_STATE_TERMINATED;
        pthread_rwlock_unlock(&vm->server->rwlock);
    }
    return NULL;
    
    
}


void *run_conf_file(void *args){
    run_file_args * run_file_info = (run_file_args *) args;
    int fd = run_file_info->fd;
    DataCenter *dc = run_file_info->dc;
    double personal_cores = run_file_info->personal_cores;
    
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

            if (datacenter_define_VM(dc, &vmtype) != 0) {
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

            if (datacenter_reserve(dc, &reservation) != 0) {
                fprintf(stderr, "Failed to reserve VMs.\n");
                continue;
            }

            printf("Reservation made successfully!\n");

            // criar pasta da reserva
            char reserve_dir[MAX_PATH_SIZE];
            if (new_dir_reserve((&reservation)->id, reserve_dir) < 0) {
                continue;
            }

            for (size_t vm_index = 0; vm_index < reservation.num_vms;
                    vm_index++) {
                VM *vm = reservation.vms[vm_index];
                char vm_dir[MAX_PATH_SIZE];
                // criar pasta da VM
                if (new_dir_vm(vm->id, reserve_dir, vm_dir) < 0) {
                    continue;
                }
                // transferir pasta inputs->pasta vm
                if (transferir_inputs(vm->type->input_folder, vm_dir)) {
                    continue;
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

            if (datacenter_execute(dc, id, personal_cores) != 0) {
                fprintf(stderr, "Failed to execute reservation.\n");
                continue;
            }

            printf("Finished reservation execution!\n");

            break;

        case CMD_LIST:
            if (datacenter_list(dc) != 0) {
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
    return NULL;
}

int main(int argc, char **argv) {
    DataCenter dc;
    datacenter_init(&dc);

    if (argc != 8) {
        fprintf(stderr,
                "Usage: %s <servers> <ram> <disk> <cpus> "
                "<personal_cpu_cores> <input_dir> <max_threads>\n",
                argv[0]);
        return 1;
    }

    size_t servers;
    size_t ram;
    size_t disk;
    double cpu;
    double personal_cores;
    size_t max_threads;

    if (parse_size_t_arg(argv[1], &servers) != 0 ||
        parse_size_t_arg(argv[2], &ram) != 0 ||
        parse_size_t_arg(argv[3], &disk) != 0 ||
        parse_double_arg(argv[4], &cpu) != 0 ||
        parse_double_arg(argv[5], &personal_cores) != 0 ||
        parse_size_t_arg(argv[7], &max_threads) != 0) {
        fprintf(stderr, "Invalid command line arguments.\n");
        return 1;
    }

    if (!path_exists(argv[6])) {
        fprintf(stderr, "Invalid input directory.\n");
        return 1;
    }

    Resources resources = {.ram = ram,
                           .disk = disk,
                           .cpu = cpu,
                           .personal_cpu_cores = personal_cores};

    if (datacenter_configure(&dc, servers, &resources) != 0) {
        fprintf(stderr, "Failed to configure Data Center.\n");
        return 1;
    }

    //Garantir que /tmp/Cloudist existe, caso contrário cria-a
    if(mkdir("/tmp/CloudIST", 0777) == -1 && errno != EEXIST){
        perror("Failed to create /tmp/CloudIST");
        return 1;
    }

    // Cria array de ponteiros para structs dirent
    struct dirent **file_name_list;
    int n = scandir(argv[6], &file_name_list, filter_conf, alphasort);
    if (n < 0) {
        perror("Failed to scan input directory");
        return EXIT_FAILURE;
    }

    
    if(max_threads == 0){
        fprintf(stderr, "max_threads must be at least 1.\n");
        return 1;
    }
    
    
    pthread_t tid_global;
    pthread_create(&tid_global, NULL, waitGlobal, &dc);
    pthread_t tid[max_threads];
    run_file_args args[max_threads];
    for(int base = 0; base < n; base += (int)max_threads){
        //ficheiros por lote
        int lote = n - base;
        if(lote > (int)max_threads){
            lote = (int)max_threads;
        }
        for (int i = 0; i < lote; i++) {
            
            // Concatenação da dir com o ficheiro para criar o file_path para o open
            char file_path[MAX_PATH_SIZE];
            snprintf(file_path, sizeof(file_path), "%s/%s", argv[6],
            file_name_list[base + i]->d_name);
            int fd = my_open(file_path);
            
            //transformar daqui para baixo numa função para poder chamar pthread com essa função
            args[i] = (run_file_args){.dc = &dc, .fd = fd, .personal_cores = personal_cores};
            pthread_create(&tid[i], NULL, run_conf_file, &args[i]);
        }

        for(int i = 0; i < lote; i++){
            if(pthread_join(tid[i], NULL) != 0){
                printf("Error joining thread\n");
                return EXIT_FAILURE;
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