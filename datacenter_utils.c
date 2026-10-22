#include "datacenter_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pthread.h>

VMType *VMType_exists(DataCenter *dc, const char *type_id) {
    for (size_t i = 0; i < dc->num_vm_types; i++) {
        if (strncmp(type_id, dc->vm_types[i].id, MAX_STRING_SIZE) == 0) {
            return &dc->vm_types[i];
        }
    }
    return NULL;
}

/**
 * Checks if a reservation with the given id already exists in the Data Center.
 *
 * @param dc Pointer to Data Center.
 * @param reservation_id Id of the reservation being checked.
 *
 * @return 1 if the reservation exists.
 * @return 0 if the reservation does not exist.
 */
static int reservation_exists(DataCenter *dc, const char *reservation_id) {
    //lock leitura do dc
    pthread_rwlock_rdlock(&dc->rwlock);
    for (size_t i = 0; i < dc->num_reservations; i++) {
        if (strncmp(reservation_id, dc->reservations[i].id, MAX_STRING_SIZE) ==
        0) {
            pthread_rwlock_unlock(&dc->rwlock);
            return 1;
        }
    }
    pthread_rwlock_unlock(&dc->rwlock);
    return 0;
}

int reservation_validate(DataCenter *dc, Reservation *reservation) {
    if (reservation_exists(dc, reservation->id)) {
        fprintf(stderr, "Reservation with id %s already exists.\n",
                reservation->id);
        return 1;
    }

    Resources available[dc->num_servers];
    size_t hosted_count[dc->num_servers];
    size_t vms_count = 0;

    /* Copy current server resources and hosted_vms so we can simulate the
     * allocation. */
    for (size_t i = 0; i < dc->num_servers; i++) {
        available[i] = dc->servers[i].available;
        hosted_count[i] = dc->servers[i].num_hosted_vms;
    }

    for (size_t i = 0; i < reservation->items_count; i++) {
        ReservationItem *item = &reservation->items[i];

        if (item->amount == 0) {
            fprintf(stderr, "Reservation item must request at least one VM.\n");
            return 1;
        }

        VMType *vm_type = VMType_exists(dc, item->vm_type_id);

        if (vm_type == NULL) {
            fprintf(stderr, "VM of type %s does not exist\n", item->vm_type_id);
            return 1;
        }

        if (item->server_id == 0 || item->server_id > dc->num_servers) {
            fprintf(stderr, "Invalid server in reservation.\n");
            return 1;
        }

        for (size_t j = 0; j < item->amount; j++) {
            if (vms_count >= MAX_RESERVATION_VMS) {
                fprintf(stderr, "Reached max of VMS for a reservation.\n");
                return 1;
            }

            if (hosted_count[item->server_id - 1] >= MAX_HOSTED_VMS) {
                fprintf(stderr,
                        "Server \"%zu\" reached the maximum number of hosted "
                        "VMs.\n",
                        item->server_id);
                return 1;
            }

            if (!resources_can_fit(available[item->server_id - 1],
                                   vm_type->required)) {
                fprintf(stderr,
                        "Server \"%zu\" does not have enough resources.\n",
                        item->server_id);
                return 1;
            }

            resources_sub(&available[item->server_id - 1], vm_type->required);
            hosted_count[item->server_id - 1]++;
            vms_count++; // One VM "reserved"
        }

        // Attach vm_type to item for later use.
        item->vm_type = vm_type;
    }

    return 0;
}

/**
 * Rolls back the changes made by a reservation operation.
 *
 * @param reservation Pointer to the reservation being rolled back.
 * @param initial_vms Number of VMs that existed before the operation.
 */
static void reservation_rollback(Reservation *reservation, size_t initial_vms) {
    while (reservation->num_vms > initial_vms) {
        VM *vm = reservation->vms[reservation->num_vms - 1];

        Server *server = vm->server;

        // Restore resources
        resources_add(&server->available, vm->type->required);

        // Remove VM from server hosted list
        server->num_hosted_vms--;

        server->hosted_vms[server->num_hosted_vms] = NULL;

        // Remove VM from reservation
        reservation->num_vms--;

        reservation->vms[reservation->num_vms] = NULL;

        free(vm);
    }
}

int reservation_commit(DataCenter *dc, Reservation *reservation) {
    // Keep track of how many vms existed before.
    size_t initial_vms = reservation->num_vms;

    for (size_t i = 0; i < reservation->items_count; i++) {
        ReservationItem *item = &reservation->items[i];
        Server *server = &dc->servers[item->server_id - 1];

        for (size_t j = 0; j < item->amount; j++) {
            VM *new_vm = calloc(1, sizeof(VM));
            if (new_vm == NULL) {
                fprintf(stderr, "Failed to allocate VM.\n");
                reservation_rollback(reservation, initial_vms);
                return 1;
            }

            snprintf(new_vm->id, MAX_VM_ID_STRING, "%s_%zu", reservation->id,
                     reservation->num_vms + 1);

            new_vm->type = item->vm_type;
            new_vm->server = server;
            new_vm->state = VM_STATE_RESERVED;

            resources_sub(&server->available, item->vm_type->required);

            server->hosted_vms[server->num_hosted_vms++] = new_vm;
            reservation->vms[reservation->num_vms++] = new_vm;
        }
    }

    reservation->state = RES_STATE_PENDING;

    return 0;
}

Reservation *find_pending_reservation(DataCenter *dc,
                                      const char *reservation_id) {
    Reservation *res = NULL;
    
    //lock leitura dc
    pthread_rwlock_rdlock(&dc->rwlock);
    for (size_t i = 0; i < dc->num_reservations; i++) {
        if (strcmp(dc->reservations[i].id, reservation_id) == 0) {
            res = &dc->reservations[i];
            break;
        }
    }
    pthread_rwlock_unlock(&dc->rwlock);

    if (!res) {
        fprintf(stderr, "Reservation ID %s not found.\n", reservation_id);
        return NULL;
    }

    if (res->state != RES_STATE_PENDING) {
        fprintf(stderr, "Reservation ID %s is not in a pending state.\n",
                reservation_id);
        return NULL;
    }

    return res;
}

void reservation_destroy(DataCenter *dc, Reservation *reservation) {
    // Free every VM belonging to the reservation.
    //lock read write servers

    // lista de pointers servidores envolvidos
    Server * envolved_servers[reservation->num_vms]; // worst case scenario todas as vms trabalham num server diferente
    for (int i = 0; i < (int)reservation->num_vms; i++) {
        envolved_servers[i] = reservation->vms[i]->server;
    }

    //ordenar e tirar repetidos
    int num_envolved_servers = (int)sort_and_dedup_server_ptrs(envolved_servers, reservation->num_vms);
    
    // lock todos os servidores involvidos
    for(int i = 0; i < num_envolved_servers; i++){
        pthread_rwlock_wrlock(&envolved_servers[i]->rwlock);
    }

    for (size_t i = 0; i < reservation->num_vms; i++) {
        VM *vm = reservation->vms[i];
        Server *server = vm->server;

        // Return the VM resources back to the hosting server.
        resources_add(&server->available, vm->type->required);

        // Remove the VM from the server's hosted VM list.
        for (size_t j = 0; j < server->num_hosted_vms; j++) {
            if (server->hosted_vms[j] == vm) {
                // Shift the remaining VMs one position to the left.
                memmove(&server->hosted_vms[j], &server->hosted_vms[j + 1],
                        (server->num_hosted_vms - j - 1) * sizeof(VM *));

                server->num_hosted_vms--;
                break;
            }
        }

        free(vm);
        reservation->vms[i] = NULL;
    }
    // unlock servers
    for(int i = 0; i < num_envolved_servers; i++){
        pthread_rwlock_unlock(&envolved_servers[i]->rwlock);
    }

    reservation->num_vms = 0;
    
    // Find the reservation inside the Data Center reservation list.
    size_t idx;
    //lock dc escrita e leitura
    pthread_rwlock_wrlock(&dc->rwlock);
    for (idx = 0; idx < dc->num_reservations; idx++) {
        if (&dc->reservations[idx] == reservation)
        break;
    }
    
    // Reservation not found (should never happen).
    if (idx == dc->num_reservations)
    return;
    
    // Remove the reservation by shifting the remaining ones.
    memmove(&dc->reservations[idx], &dc->reservations[idx + 1],
        (dc->num_reservations - idx - 1) * sizeof(Reservation));
        
        dc->num_reservations--;
        
    pthread_rwlock_unlock(&dc->rwlock);
}

void spawn_vm_child(VM *vm, DataCenter *dc, double personal_cores) {
    (void)vm; // To avoid warning.

    // TODO: Limit RAM, DISK and use exec with cpulimit.
    vm->state = VM_STATE_RUNNING;
    vm->destroyed= 0;
    double cpu_por_maquina = vm->server->total.cpu;
    double cpu_por_vm = vm->type->required.cpu;
    int num_maquinas = (int)dc->num_servers;
    double cpu_limit_perc =
        100 * personal_cores * cpu_por_vm / (cpu_por_maquina * num_maquinas);

    char limit_str[16];
    snprintf(limit_str, sizeof(limit_str), "%f", cpu_limit_perc);

    execlp("cpulimit", "cpulimit", "-q", "-f", "-l", limit_str, "--",
           vm->type->exec_path, NULL);
    fprintf(stderr, "VM execution failed.\n");
}

int spawn_all_vms(Reservation *res, DataCenter *dc, double personal_cores) {
    for (size_t i = 0; i < res->num_vms; i++) {
        VM *vm = res->vms[i];

        // TODO: Implement fork code. Set VM PID and update VM state to running.
        if ((vm->pid = fork()) == 0) {
            struct rlimit rl1;
            rl1.rlim_cur = vm->type->required.ram;
            rl1.rlim_max = vm->type->required.ram;
            if (setrlimit(RLIMIT_AS, &rl1) == -1) {
                printf("Erro ao definir o limite");
                return 1;
            }
            struct rlimit rl2;
            rl2.rlim_cur = vm->type->required.disk;
            rl2.rlim_max = vm->type->required.disk;
            if (setrlimit(RLIMIT_FSIZE, &rl2) == -1) {
                printf("Erro ao definir o limite");
                return 1;
            }
            spawn_vm_child(vm, dc, personal_cores);
        }
    }

    return 0;
}

void wait_for_all_vms(Reservation *res) {
    
    for (size_t i = 0; i < res->num_vms; i++) {
        int status;
        wait(&status);
        if (status < 0) {
            printf("VM execution finished with -1");
        }
        res->vms[i]->state = VM_STATE_TERMINATED;
    }
}

const char *vm_state_to_string(VMState state) {
    switch (state) {
    case VM_STATE_RESERVED:
        return "RESERVED";
    case VM_STATE_RUNNING:
        return "RUNNING";
    case VM_STATE_TERMINATED:
        return "TERMINATED";
    default:
        return "UNKNOWN";
    }
}

const char *res_state_to_string(ReservationState state) {
    switch (state) {
    case RES_STATE_PENDING:
        return "PENDING";
    case RES_STATE_RUNNING:
        return "RUNNING";
    case RES_STATE_FINISHED:
        return "FINISHED";
    default:
        return "UNKNOWN";
    }
}

// Comparador para um array de Server*
int compare_server_ptrs_by_id(const void *a, const void *b) {
    // a e b apontam para ELEMENTOS do array, e cada elemento é um Server*
    const Server *sa = *(Server * const *)a;
    const Server *sb = *(Server * const *)b;

    if (sa->id < sb->id) return -1;
    if (sa->id > sb->id) return 1;
    return 0;
}

size_t sort_and_dedup_server_ptrs(Server *ptrs[], size_t n) {
    if (n <= 1) return n;

    qsort(ptrs, n, sizeof(Server *), compare_server_ptrs_by_id);

    size_t j = 1;
    for (size_t i = 1; i < n; i++) {
        if (ptrs[i]->id != ptrs[j - 1]->id) {
            ptrs[j++] = ptrs[i];
        }
    }
    return j;
}

void *scan_terminated_vms(void *args){
    Reservation *res = args;
    int counter = 0;
    while(counter < res->num_vms){
        for(int i = 0; i < res->num_vms; i++){
            if(res->vms[i]->state == VM_STATE_TERMINATED && res->vms[i]->destroyed == 0){
                Server *server = res->vms[i]->server;
                pthread_rwlock_wrlock(&server->rwlock);
                Resources finished_resources = res->vms[i]->type->required; 
                resources_add(&server->available, finished_resources);
                pthread_rwlock_unlock(&server->rwlock);
                counter++;
                res->vms[i]->destroyed = 1;
                if(counter == res->num_vms) break;
            }
        }
    }
    res->state = RES_STATE_FINISHED;
    return NULL;


}