#include "header.h"



void init(){

    int count, i, j, x;
    char buffer[MAX_CHARS],buffer_aux[MAX_CHARS] ,*token;
    FILE *config;
    file = fopen("log.txt", "w");
    open_sem();
    write_log("---INICIO DO PROGRAMA---");
    config = fopen("config.txt", "r");
    if(config){
        //__________________max_x, max_y__________________
        fscanf(config, "%d %d\n", &max_x, &max_y);
        //__________________BASES__________________
        bases[0].x = 0;	bases[0].y = 0;
        bases[1].x = max_x;	bases[1].y = 0;
        bases[2].x = 0;	bases[2].y = max_y;
        bases[3].x = max_x;	bases[3].y = max_y;


        //_________________tipos_de_produtos_________________
        fgets(buffer, sizeof(buffer), config);
        strcpy(buffer_aux, buffer);
        count = 0;
        token = strtok(buffer, ",");
        while(token != NULL){
            token = strtok(NULL, ",");
            count++;
        }
        tam_tipos = count;
        tipos_de_produtos = (char**)malloc(count*sizeof(char*));
        token = strtok(buffer_aux, ",");
        i = 0;
        while(token != NULL){
            tipos_de_produtos[i] = (char*)malloc(strlen(token) + 1);
            strcpy(tipos_de_produtos[i++], token);
            token = strtok(NULL, ",");
        }

        //___________________________n_drones___________________________
        fscanf(config, "%d\n", &n_drones);

        //___________________________Drone e Thread Ids___________________________
        Thread_ids = (pthread_t*)malloc(sizeof(pthread_t)*n_drones);
        Drones = (Drone*)malloc(sizeof(Drone)*n_drones);

        //________________freq_de_abastecimento, quant, temp________________
        fscanf(config, "%d,%d,%d\n", &freq_de_abastecimento, &quant, &temp);
        freq_de_abastecimento = freq_de_abastecimento*temp;
        //_________________n_armazens e init da mem para ids_________________
        fscanf(config, "%d\n", &n_armazens);
        armazem_ids = (pid_t*)malloc(n_armazens* sizeof(pid_t));

        //________________SHARED MEMORY ARMAZENS________________
        shmid_armazem = shmget(IPC_PRIVATE, sizeof(Armazem)*n_armazens, IPC_CREAT|0700);
        if(shmid_armazem == -1){
            perror("shmget armazem error");
        }
        shared_var_armazem = (Armazem*)shmat(shmid_armazem, NULL, 0);

        //________________SHARED MEMORY ESTATISTICAS________________
        shmid_esta = shmget(IPC_PRIVATE, sizeof(Estatisticas), IPC_CREAT|0700);
        if(shmid_esta == -1){
            perror("shmget estatisticas error");
        }

        shmid_term = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT|0700);
        if(shmid_term == -1){
            perror("shmget armazem error");
        }
        shared_var_term = (int*)shmat(shmid_term, NULL, 0);
        *shared_var_term = 0;
        start_stats();  // inicializa estatísticas a 0

        //___________________________armazens___________________________
        for(i = 0; i < n_armazens; i++){
            sem_wait(mutex_wh);
            shared_var_armazem[i].id = i+1;
            fscanf(config, "%s %lf %lf", shared_var_armazem[i].nome, &shared_var_armazem[i].coordenadas.x, &shared_var_armazem[i].coordenadas.y);
            sem_post(mutex_wh);
            fgets(buffer, sizeof(buffer), config);
            strcpy(buffer_aux, buffer);
            token = strtok(buffer, " ");
            count = 0;
            while(token != NULL){
                token = strtok(NULL, " ");
                count++;
            }
            sem_wait(mutex_wh);
            shared_var_armazem[i].produtos = (Produto*)malloc(count/2 * sizeof(Produto));
            shared_var_armazem[i].count_tipos = count/2;
            sem_post(mutex_wh);
            token = strtok(buffer_aux, " ");
            j = 0;
            x = 0;
            while(token != NULL){
                if(j % 2 == 0){
                    sem_wait(mutex_wh);
                    strcpy(shared_var_armazem[i].produtos[x].tipo, token);
                    sem_post(mutex_wh);
                }else{
                    sem_wait(mutex_wh);
                    shared_var_armazem[i].produtos[x++].quantidade = atoi(token);
                    sem_post(mutex_wh);

                }
                j++;
                token = strtok(NULL, " ");
            }
        }
        //___________________MQ Init___________________
        set_msqid();

    }else
        printf("Erro ao ler config.txt\n");
    //SIGUSR1


}

void simulation_manager(){

    //Handle SIGUSR1
    struct sigaction usr_action;
    usr_action.sa_handler = Read_stats;
    //init signal mask
    sigemptyset(&usr_action.sa_mask);
    usr_action.sa_flags = 0;
    sigaction(SIGUSR1, &usr_action, NULL);


    //Handle SIGNINT (ctrl_c)
    struct sigaction sigint_action;
    sigint_action.sa_handler = Terminate;
    //init signal mask
    sigemptyset(&sigint_action.sa_mask);
    sigint_action.sa_flags = 0;
    sigaction(SIGINT, &sigint_action, NULL);

    //Simulation manager bloqueia todos sinais excepto SIGUSR2 que é preciso na Central
    sigset_t mask;
    sigfillset(&mask);
    sigdelset(&mask, SIGINT);
    sigdelset(&mask, SIGUSR1);
    sigdelset(&mask, SIGUSR2);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    //Criação de todos os processos armazém
    int i;
    for(i = 0; i < n_armazens; i++) {
        armazem_ids[i] = fork();
        if (armazem_ids[i] == 0) {
            Armazens();
        }
    }

    printf("aqui depois armazens\n");

    //Criação do processo Central, named pipe e threads drone
    if(fork() == 0){
        Central();
        exit(0);
    }
    printf("aqui depois central\n");

    refill();

}

void set_msqid(){
    msqid = msgget(IPC_PRIVATE, IPC_CREAT|0700);
    printf("aqui msqid\n");
}

void open_sem(){

    sem_unlink(SEM_MUTEX);
    sem_unlink(SEM_MUTEX_ESTA);
    sem_unlink(SEM_MUTEX_WH);
    mutex_write = sem_open(SEM_MUTEX, O_CREAT, 0700,1);
    mutex_wh = sem_open(SEM_MUTEX_WH, O_CREAT, 0700,1);
    mutex_esta = sem_open(SEM_MUTEX_ESTA, O_CREAT, 0700,1);
    if(mutex_write == SEM_FAILED) {
        perror("a criar o semaforo...");
    }
    if(mutex_wh == SEM_FAILED) {
        perror("a criar o semaforo...");
    }
    if(mutex_esta == SEM_FAILED) {
        perror("a criar o semaforo...");
    }
}

void *start_stats(){
    shared_var_esta = (Estatisticas*)shmat(shmid_esta, NULL, 0);
    shared_var_esta->tot_enc_drones = 0;
    shared_var_esta->tot_prod_carregados_armazens = 0;
    shared_var_esta->tot_enc_entregues = 0;
    shared_var_esta->tot_prod_entregues = 0;
    shared_var_esta->tempo_medio_conclusao = 0;
    return shared_var_esta;
}


void Terminate_Central(int signum){
    close(input_pipe);
    // free drones
    delete_drones();
    // Release pipe
    unlink(PIPE_NAME);
    *shared_var_term = 1;
    exit(0);
}

void Terminate(int signum){
    write_log("CTRL-C RECEBIDO. A TERMINAR...");
    signal(SIGUSR1, SIG_IGN);

    kill(central_id, SIGUSR2);
    wait(NULL);

    mq_to_armazem out_msg;
    out_msg.tipo = MORTE;
    int i;

    //Fechar os aramzens (mandar msg por MQ)
    for(i = 0; i < n_armazens; i++){
        out_msg.id_armazem = i + 1;
        msgsnd(msqid, &out_msg, sizeof(mq_to_armazem), 0);
        wait(NULL);
    }

    //Remove MQ id
    msgctl(msqid, IPC_RMID, NULL);
    // Mark memory segment for destruction // Detach shared memory
    shmdt(shared_var_armazem);
    shmdt(shared_var_esta);
    shmdt(shared_var_term);
    shmctl(shmid_armazem, IPC_RMID, NULL);
    shmctl(shmid_esta, IPC_RMID, NULL);
    shmctl(shmid_term, IPC_RMID, NULL);
    write_log("---FIM DO PROGRAMA---");
    //Fecha semáforos
    sem_close(mutex_write);
    sem_close(mutex_esta);
    sem_close(mutex_wh);
    sem_close(mutex_end);
    sem_unlink(SEM_MUTEX);
    sem_unlink(SEM_MUTEX_ESTA);
    sem_unlink(SEM_MUTEX_WH);
    sem_unlink(SEM_MUTEX_END);

    //kill(0, SIGKILL);
    exit(0);

}

void Read_stats(int signum){
    sem_wait(mutex_esta);
    printf("Número total de encomendas atribuídas a drones = %d\n", shared_var_esta->tot_enc_drones);
    printf("Número total de produtos carregados de armazéns = %d\n", shared_var_esta->tot_prod_carregados_armazens);
    printf("Número total de encomendas entregues = %d\n", shared_var_esta->tot_enc_entregues);
    printf("Número total de produtos entregues = %d\n", shared_var_esta->tot_prod_entregues);
    printf("Tempo médio para conclusão de uma encomenda = %d\n", shared_var_esta->tempo_medio_conclusao);
    sem_post(mutex_esta);
}

void write_log(char *frase){
    char buff[100];
    struct tm *cur_time;
    sem_wait(mutex_write);
    time_t now = time (0);
    cur_time = gmtime (&now);
    strftime (buff, sizeof(buff), "%H:%M:%S ", cur_time);
    strcat(buff, frase);
    fprintf(file, "%s\n", buff);
    fflush(file);
    sem_post(mutex_write);
}

void stock_update(int request, Armazem *shared_var_armazem, int i, int x){
    int stock=shared_var_armazem[i].produtos[x].quantidade;
    if(stock - request < 0){
        request = stock;
    }
    shared_var_armazem[i].produtos[x].quantidade = stock-request;
    shared_var_esta->tot_enc_drones++;
    shared_var_esta->tot_prod_carregados_armazens += request;
    printf("\nArmazem: %d | Produto: %s\nTinha-> %d\nAgora tem-> %d\n\n", shared_var_armazem[i].id, shared_var_armazem[i].produtos[x].tipo, stock, shared_var_armazem[i].produtos[x].quantidade);
}

//Criação named_pipe
void Pipe() {
    if (mkfifo(PIPE_NAME, O_CREAT | O_EXCL | 0600) < 0 && errno != EEXIST) {
        perror("Erro a criar fifo.");
        exit(0);
    }
}

void move_towards_nearest_base(Drone *n_drone){
    double nearest, dist, x, y;
    nearest = distance(n_drone->posicao.x, n_drone->posicao.y, bases[0].x, bases[0].y);
    x = bases[0].x;
    y = bases[0].y;
    for(int i = 0; i<4; i++) {
        dist = distance(n_drone->posicao.x, n_drone->posicao.y, bases[i].x, bases[i].y);
        if (nearest > dist){
            nearest = dist;
            x = bases[i].x;
            y = bases[i].y;
        }
    }
    int ans;
    //Ir até base mais proxima mas estar sempre a verificar se não é atribuida uma nova encomenda
    do{
        if(!n_drone->ocupado){
            ans = move_towards(&n_drone->posicao.x, &n_drone->posicao.y, x, y);
            if(ans == -2){
                return;
            }
            usleep(temp * 1000000);
        }else return;
    }while(ans != -1);
}

void *drone(void* new_drone){
    char log [MAX_CHARS];
    int flag = 0;
    Drone *drone = ((Drone*)new_drone);
    printf("Drone <%d> encontra-se em (%.2lf, %.2lf) e esta parado.\n", drone->id, drone->posicao.x, drone->posicao.y);
    sem_wait(mutex_end);
    while(*shared_var_term == 1){
        pthread_mutex_lock(&drone_mutex);
        if(flag == 0){
            pthread_cond_wait(&drone_cond, &drone_mutex);
            //recebe ordem
            flag = 1;
            drone->ocupado = 1;
        }
        else{
            while(drone->ocupado==1){
                move_towards_armazem(*drone, drone->encomenda->armazem);

                mq_to_armazem out_msg;
                mq_from_armazem in_msg;
                //MQ para notificar o armazem da chegada
                out_msg.id_armazem = drone->encomenda->armazem.id;
                out_msg.tipo = CHEGADA;
                out_msg.id_encomenda = drone->encomenda->id;
                out_msg.quant = drone->encomenda->produto.quantidade;
                msgsnd(msqid, &out_msg, sizeof(mq_to_armazem), 0);

                //Esperar a resposta do armazem
                msgrcv(msqid, &in_msg, 0, drone->encomenda->id, 0);

                move_towards_destination(*drone);

                sprintf(log, "Drone [%d] enctregou a encomenda [%d]!\n", drone->id, drone->encomenda->id);
                write_log(log);

                //Atualizar estatisticas
                shared_var_esta->tot_enc_entregues++;
                shared_var_esta->tot_prod_entregues += drone->encomenda->produto.quantidade;
                //Libertar o drone
                drone->ocupado = 0;
                drone->encomenda = NULL;
                move_towards_nearest_base(drone);
                sleep(1);
            }
        }
    }
    sem_post(mutex_end);
    pthread_cond_broadcast(&drone_cond);
    pthread_mutex_unlock(&drone_mutex);

    printf("Drone %d a ir embora!\n", drone->id);
    pthread_exit(NULL);
}

void move_towards_armazem(Drone drone, Armazem armazem){
    int ans;
    do{
        printf("Drone <%d> encontra-se em (%.2lf, %.2lf) e esta a caminho do Armazem %d (%.2lf, %.2lf).\n", drone.id, drone.posicao.x, drone.posicao.y, armazem.id,armazem.coordenadas.x, armazem.coordenadas.y);
        ans = move_towards(&drone.posicao.x, &drone.posicao.y, armazem.coordenadas.x, armazem.coordenadas.y);
        if(ans == -2){
            return;
        }
        usleep(temp * 1000000);
    }while(ans != -1);
    printf("Drone [%d] chegou ao armazem [%d]!\n", drone.id, armazem.id);
}

void move_towards_destination(Drone drone){
    int ans;
    do{
        printf("Drone <%d> encontra-se em (%.2lf, %.2lf) e esta a caminho de (%.2lf, %.2lf).\n", drone.id, drone.posicao.x, drone.posicao.y, drone.encomenda->destino.x, drone.encomenda->destino.y);
        ans = move_towards(&drone.posicao.x, &drone.posicao.y, drone.encomenda->destino.x, drone.encomenda->destino.y);
        if(ans == -2){
            return;
        }
        usleep(temp * 1000000);
    }while(ans != -1);
    printf("Drone [%d] entregou a encomenda [%d]!\n", drone.id, drone.encomenda->id);
}

void choose_closest_drone(Encomenda *encomenda){
    double nearest, total, dist1, dist2, x, y;
    DroneList *cur = drone_list->next;
    nearest = distance(cur->drone->posicao.x, cur->drone->posicao.y, encomenda->armazem.coordenadas.x, encomenda->armazem.coordenadas.y);
    x = shared_var_armazem[0].coordenadas.x;
    y = shared_var_armazem[0].coordenadas.y;
    while(cur->next != NULL){
        dist1 = distance(cur->drone->posicao.x, cur->drone->posicao.y, encomenda->armazem.coordenadas.x, encomenda->armazem.coordenadas.y);
        dist2 = distance(encomenda->armazem.coordenadas.x, encomenda->armazem.coordenadas.y, encomenda->destino.x, encomenda->destino.y);
        total = dist1 + dist2;
        if (nearest > total){
            nearest = total;
            x = encomenda->armazem.coordenadas.x;
            y = encomenda->armazem.coordenadas.y;
        }
    }
    int ans;
    do{
        if(!cur->drone->ocupado){
            ans = move_towards(&cur->drone->posicao.x, &cur->drone->posicao.y, x, y);
            if(ans == -2){
                return;
            }
            usleep(temp * 1000000);
        }else return;
    }while(ans != -1);
}

void create_drones() {
    DroneList * cur_drone = drone_list;
    ThreadList * cur_thread = thread_id_list->next;
    ThreadList * node_thread;
    DroneList * node_drone;
    Drone * newdrone;
    int base;

    while (cur_drone->next != NULL) {
        cur_drone = cur_drone->next;
    }
    while(cur_thread->next != NULL){
        cur_thread = cur_thread->next;
    }

    for(int i = 0; i < n_drones; i++) {

        newdrone = malloc(sizeof(Drone));

        cur_drone->next = malloc(sizeof(DroneList));
        base = rand() % 4;
        newdrone->id = i + 1;
        newdrone->ocupado = 0;
        newdrone->posicao.y = bases[base].y;
        newdrone->posicao.x = bases[base].x;
        node_drone = malloc(sizeof(DroneList));
        node_thread = malloc(sizeof(ThreadList));
        cur_drone->next = node_drone;
        cur_thread->next = node_thread;
        node_thread->id = newdrone->id;
        node_thread->next = NULL;
        node_drone->drone = newdrone;
        node_drone->next = NULL;
        cur_drone = node_drone;
        cur_thread = node_thread;
        if(pthread_create(&Thread_ids[i], NULL, drone, newdrone)){
            perror("Erro ao criar as threads.");
            exit(0);
        }
        usleep(100000);
    }
    printf("[CENTRAL] Todos os drones criados.\n");
}

DroneList *create_list() {        // Cria lista com header
    DroneList *list = NULL;
    list = malloc(sizeof(DroneList));
    list->next = NULL;
    return list;
}

ThreadList *create_thread_list() {        // Cria lista com header
    ThreadList *list = NULL;
    list = malloc(sizeof(ThreadList));
    list->next = NULL;
    return list;
}

void delete_drones(){
    DroneList* cur_drone = drone_list;
    ThreadList* cur_thread = thread_id_list;
    while(cur_drone != NULL){
        cur_drone = drone_list->next;
        free(drone_list);
        drone_list = cur_drone;
    }

    while(cur_thread != NULL){
        cur_thread = thread_id_list->next;
        pthread_cancel(cur_thread);
        pthread_join(cur_thread, NULL);
        free(thread_id_list);
        thread_id_list = cur_thread;
    }
}

void Central(){
    char buf[MAX_CHARS];
    char bufcpy[MAX_CHARS];
    char frase[MAX_CHARS];
    char *command;
    int n, id = 1;
    int n_drones;
    Encomenda *nova_encomenda;

    central_id = getpid();
    //Ignorar sinais SIGINT e SIGUSR1
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGUSR1);
    sigprocmask(SIG_BLOCK, &mask, NULL);

    //Tratar do SIGUSR2
    struct sigaction fim;
    fim.sa_handler = Terminate_Central; //funcao por acabar, falta lista ligada das threads drones
    sigemptyset(&fim.sa_mask);
    fim.sa_flags = 0;
    sigaction(SIGUSR2, &fim, NULL);

    //Criação do named pipe
    Pipe();
    //Criação dos drones (threads)
    drone_list = create_list();
    thread_id_list = create_thread_list();
    create_drones();

    while(1){
        if ((input_pipe = open(PIPE_NAME, O_RDONLY|O_NONBLOCK)) < 0) {
            perror("Erro a abrir fifo.");
        }
        n = read(input_pipe, buf, sizeof(buf));
        strcpy(bufcpy, buf);
        bufcpy[n-1] = '\0';
        command = strtok(buf, " ");
        if(strcmp(command, "ORDER") == 0){
            nova_encomenda = split_command(buf);
            nova_encomenda->id = id;
            id++;
            sprintf(frase, "Encomenda Req_%d-%d recebida pela Central", nova_encomenda->id, id);
            write_log(frase);
            choose_closest_drone(nova_encomenda);
            sem_wait(mutex_esta);
            shared_var_esta->tot_enc_drones++;
            sem_post(mutex_esta);
        }
        else if(strcmp(command, "DRONE") == 0){
            command = strtok(NULL, "SET ");
            n_drones = atoi(command);
            sprintf(frase, "Introduzidos mais %d drones.", n_drones);
            write_log(frase);
            update_drones(n_drones);
        }
        else{
            sprintf(frase, "Comando inválido introduzido.");
            write_log(frase);
        }

    }
}

void update_drones(int quant){
    n_drones += quant;
    DroneList * cur_drone = drone_list;
    ThreadList * cur_thread = thread_id_list->next;
    ThreadList * node_thread;
    DroneList * node_drone;
    Drone * newdrone;
    int base;

    while (cur_drone->next != NULL) {
        cur_drone = cur_drone->next;
    }
    while(cur_thread->next != NULL){
        cur_thread = cur_thread->next;
    }

    for(int i = 0; i < n_drones; i++) {
        newdrone = malloc(sizeof(Drone));
        cur_drone->next = malloc(sizeof(DroneList));
        base = rand() % 4;
        newdrone->id = i + 1;
        newdrone->ocupado = 0;
        newdrone->posicao.y = bases[base].y;
        newdrone->posicao.x = bases[base].x;
        node_drone = malloc(sizeof(DroneList));
        node_thread = malloc(sizeof(ThreadList));
        cur_drone->next = node_drone;
        cur_thread->next = node_thread;
        node_thread->id = newdrone->id;
        node_thread->next = NULL;
        node_drone->drone = newdrone;
        node_drone->next = NULL;
        cur_drone = node_drone;
        cur_thread = node_thread;
        if(pthread_create(cur_thread, NULL, drone, newdrone)){
            perror("Erro ao criar as threads.");
            exit(0);
        }
        usleep(100000);
    }
}


Encomenda *split_command(char *command) {
    Encomenda *encomenda = malloc(sizeof(Encomenda));;
    char* token;
    token = strtok(command, "ORDER Req_");        // Req_x
    encomenda->id = atoi(token);
    token = strtok(NULL, " , ");                  // prod:
    token = strtok(NULL, ", ");                   // Tipo
    strcpy(encomenda->produto.tipo, token);
    token = strtok(NULL, ", ");                   // Quantidade
    encomenda->produto.quantidade = atoi(token);
    token = strtok(NULL, " to: ");                //x
    encomenda->destino.x = atoi(token);
    token = strtok(NULL, ", ");                   //y
    encomenda->destino.y = atoi(token);
    return encomenda;
}

void Armazens(int i){
    int x;
    Armazem *armazem_atual;
    Produto *produto_atual;
    char buffer[512];
    //___________________Armazem bloqueia todos os sinais_________________
    sigset_t block;
    sigfillset(&block);
    sigprocmask(SIG_BLOCK, &block, NULL);
    //____________________________________________________________________

    armazem_atual = &shared_var_armazem[i];

    sem_wait(mutex_wh);
    printf("Armazem %d criado em (%.2lf,%.2lf) e tem %d tipos de produtos diferentes.\n",armazem_atual->id, armazem_atual->coordenadas.x, armazem_atual->coordenadas.y, armazem_atual->count_tipos);
    //O PID de cada um dos processos armazém criados e o seu W_NO deverão ser escritos no ecrã e no log.
    sprintf(buffer, "Armazém com o numero %d e PID %d criado.",armazem_atual->id, getpid());
    sem_post(mutex_wh);
    write_log(buffer);

    while(1){
        mq_to_armazem in_msg;
        mq_from_armazem out_msg;

        //Resposta do drone
        msgrcv(msqid, &in_msg, sizeof(mq_to_armazem), armazem_atual->id, 0);

        //Chegada de drones
        if(in_msg.tipo == CHEGADA){
            sem_wait(mutex_wh);
            printf("Armazem %s [id = %d] pronto para carregamento.\n",armazem_atual->nome, armazem_atual->id);
            sem_post(mutex_wh);
            sleep(in_msg.quant * temp);
            sem_wait(mutex_esta);
            shared_var_esta->tot_prod_carregados_armazens += in_msg.quant;
            sem_post(mutex_esta);
            //Responder ao drone
            out_msg.id_encomenda = in_msg.id_encomenda;
            msgsnd(msqid, &out_msg, sizeof(mq_from_armazem), 0);

            sem_wait(mutex_wh);
            printf("Armazem %s [id = %d] acabou o carregamento.\n",armazem_atual->nome, armazem_atual->id);
            sem_post(mutex_wh);

        //Refill dos armazens
        } else if(in_msg.tipo == REFILL){
            sem_wait(mutex_wh);
            //Encontrar o produto
            for(x = 0; x < armazem_atual->count_tipos; x++){
                produto_atual = &armazem_atual->produtos[x];
                if(strcmp(produto_atual->tipo, in_msg.produto) == 0){
                    //Atualizar stock
                    produto_atual->quantidade += in_msg.quant;
                    sprintf(buffer, "Armazem %s [id = %d] recarregado.\n", armazem_atual->nome, armazem_atual->id);
                    sem_post(mutex_wh);
                    write_log(buffer);
                }
            }
        }
        else{
            sem_wait(mutex_wh);
            sprintf(buffer, "Armazem %s [id = %d] fechado.\n",armazem_atual->nome, getpid());
            sem_post(mutex_wh);
            write_log(buffer);
            exit(0);
        }
    }
}


void refill(){
    int i = 0, id, x, prod;
    Produto produto;
    time_t t;
    srand((unsigned) time(&t));
    mq_to_armazem out_msg;
    out_msg.tipo = REFILL;
    while(1){
        id = (i++ % n_armazens) +1;
        //Percorrer os armazens
        out_msg.id_armazem = i+1;

        //Escolher um produto random
        sem_wait(mutex_wh);
        prod = rand() % shared_var_armazem[i].count_tipos;
        sem_post(mutex_wh);
        for(x = 0; x < prod; x++){
            sem_wait(mutex_wh);
            produto = shared_var_armazem[i].produtos[x];
            sem_post(mutex_wh);
        }
        //Fazer refill do produto (comunicar com armazem a partir de MQ)
        strcpy(out_msg.produto, produto.tipo);
        out_msg.quant = quant;
        msgsnd(msqid, &out_msg, sizeof(mq_to_armazem), 0);
        usleep(freq_de_abastecimento*1000000);
    }


}

int main() {

    //leitura de config, inicializacao de variáveis, Criação da memória partilhada, criação dos semaforos, set MQ msqid
    init();

    simulation_manager();


    return 0;
}
