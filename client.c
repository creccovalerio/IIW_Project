// UDP client program 
#include <arpa/inet.h> 
#include <netinet/in.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <strings.h> 
#include <sys/socket.h>
#include <sys/types.h> 
#include <string.h>
#include <signal.h>
#include <wait.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <pthread.h>
#include <unistd.h>
#include <stdint.h>
#include <semaphore.h>
#include <sys/sem.h>
#include <sys/ipc.h>
#include <math.h>

#define flush(stdin) while(getchar() != '\n')

#define PORT 6010
#define MAXLINE 1445 
#define FALSE 0;
#define TRUE !FALSE;
#define TIMEOUT 20
#define WIND_SIZE_T 10
#define WIND_SIZE_R 100
#define DEF_LENGTH 3
#define LOSS_PROBABILITY 0.05
#define MIN_TIMEO 0.05
#define MAX_TIMEO 1.0
#define ALPHA 0.125
#define BETA 0.25 

//struct pkt da inviare
typedef struct data{
  char SYNbit[2];
  char sequence_number[10];
  char ACKbit[2];
  char ack_number[10];
  char message[MAXLINE];
  char operation_no[2];
  char FINbit[2];
  char port[6];
  char size[15];
  char port_download[6];
}t_data;


typedef struct buffered_pkt{
  char sequence_number[10];
  char ack_number[10];
  char message[MAXLINE];
  char operation_no[2];
  struct buffered_pkt *next;
  struct buffered_pkt *prev; 
}t_buffered_pkt;

typedef struct sent_packet{
    char SYNbit[2];
    char sequence_number[10];
    char ACKbit[2];
    char ack_number[10];
    char message[MAXLINE];
    char operation_no[2];
    char FINbit[2];
    int  sockfd;
    char port[6];
    char size[15];
    struct sent_packet *next;
    struct sent_packet *prev;
}t_sent_pkt;

// potremmo usare la lista dei timer perchè ogni volta che arriva un ack posso controllare 
// se ack number è presente nella lista
// ovvero seq num=ack-len
// ed elimino il timer e il pkt bufferizzato


typedef struct timer{
  timer_t  idTimer;
  char SYNbit[2];
  char sequence_number[10];
  char ACKbit[2];
  char ack_number[10];
  char message[MAXLINE];
  char operation_no[2];
  char FINbit[2];
  char port[6];
  char size[15];
#ifdef ADPTO
  float sample_value;
  float last_rto;
#endif
  struct timer *next;
  struct timer *prev;
}t_tmr;


typedef struct timer_id{
  timer_t   tidp;
  timer_t   id;
  int       sockfd;
  int       master_enabler;
  struct timer_id *next;
  struct timer_id *prev;
}t_timid;



int     num = 0;
int     num_pkt = 0;
int     a_num = 0;
int     num_of_files;
char    def_str[] = "";
int     switch_mean_d = 0;
int     switch_mean_u = 0;
int     write_on_file = 0; // flag per abilitare scrittura del contenuto del file
int     switch_fin = 0;
struct  sockaddr_in servaddr; 
struct  sockaddr_in servaddr1;
int     sockfd; 
int     client_closing = 0;
timer_t idT = 0;
t_tmr   *list_head_tmr = NULL;
t_timid *list_head_id_tmr = NULL;
t_buffered_pkt  *list_head_buffered_pkts = NULL;
t_sent_pkt    *list_head_sent_pkts = NULL;
int     sigNo = 34;  //SIGRTMIN
timer_t tid;
int     end_hs = 0;
int     acked_sent_base = 0;
#ifdef ADPTO
float   estimated_rtt = 0.5;
float   rto = 0.5;
#endif
float   sample_value = 0;
int     first_timeot_turn = 0;
time_t  est_int;
float   est_dec;
float   po;
float   sample_rtt;
float   dev;
int     receive_window = WIND_SIZE_R; // finestra di ricezione
int     last_ack = 0;  //sarebbe la sendbase dell'ack
int     expected_ack = 0;
char    *temp_buf;
char    *acknum;
char    *new_port;
int     handler_list_of_files = 0; // (after deleting of the timeout) variable will be set to 1 to handle the next packets from the server which contains the the name of the file 
int     duplicate_counter = 0; // counter per gestire la ricezione di ack duplicati
int     expected_next_seq_num = -1;
int     num_pkt_buff = 0; // numero di pacchetti che sto bufferizzando
int     temp_pkt_buf = 0; //variabile per confrontare precedente valore di num pkt buf e decidere se incrementare il counter di file ricevuti
t_data  *packets_buffer;
int     ctrlc_signal_flag = 0;
int     send_content_file_phase = 0;
int     all_ack_upload_received = 0;
int     file_content = 0;
int     ack_up_exp_switch;
int     semid;  // semaforo per dire al main thread di abilitare il 'Do you want to...' nel momento in cui termina la ricezione degli ack per l'upload
int     semid2; // semaforo per abilitare il thread ack_upload a ricevere gli ack dal server 
int     semid3; //descrittore semaforo per essere sicuri che il server abbia ricevuto ultimo ack
int     semid_filename; // descrittore per semaforo che garantisce invio del nome file prima del contenuto
int     semid_timer; // descrittore per semaforo per gestione lista dei timers
int     semid_sum_ack; // semaforo per gestione degli ack cumulativi per fare three_duplicate
int     semid_retr_turn; // semaforo per gestire il turno in caso di handling_timeout e three_duplicate
int     semid_handshake;
int     semid_window;
int     creck_stack = 0;
int     porta_send_download_file;
int     counter = 0;
int     num_files;
int     last_pack = 0;  //secondo me non server
int     closing_ack = 0;  //flag per abilitare la gestione ricezione roba inutile causa ack perso
t_data* last_sent_ack;
int     managing_list = 0; // flag per distinguere se stiamo gestendo un'operazione di list o download nel caso la recvfrom vada in error (vedi riga 1921 circa)
int     flag_last_ack_sent = 0;  //questo flag viene impostato ad uno per creare un timer più lungo ed avere così più possibilità 
                                  //di ricevere un ultimo pkt da parte del server nel caso il mio ultimo ack si sia perso
int     prev_is_upload = 0;
int     second_part_download = 0;  //flag che serve ad abilitare comportamento in base all'utilizzo del download
FILE    *file_down = NULL;
int     name_download = 0;
int     download_closing = 0;   // flag per abilitare la chiusura dopo aver debufferizzato un pkt che ha come messaggio '666'
int     upload_filename = 0;
timer_t master_IDTimer; // ID master timer
int     timerid = 0; // id della locazione di memoria
int     retr_phase = 0; //  flag per gestire ritrasmissione
int     sum_ack = 0; // variabile per contare il numero di pacchetti duplicati del client ed abilitare la three_duplicate
int     master_timer;
int     master_exists_flag = 0; // flag per dire che il master timer è effettivamente in esecuzione
int     packet_received = 0; //flag per evitare che venga fatto last_ack+2 ogni volta
int     flag_last_hs = 0;  //flag per evitare invio nel caso scatti ultimo timer
int     first_msg_close = 0; //flag per abilitare lettura ackbit del client alla mia richiesta di connessione
int     flag_final_timer = 0; 
int     flag_size_upload = 0;
int     flag_close_wait = 0; // flag per abilitare invio del FINbit lato client quando è il server che fa ctrl+c (starting LAST ACK phase)
char*   numoffiles = NULL;
long int file_size;
long int sizef;
int     flag_last_cl_msg_nsend = 0;
char path[MAXLINE];

union semun{
  int val;
  struct semid_ds *buf;
  unsigned short *array;
  struct sem_info *_buf;
};


void choice_menu(){
  printf("\n\t***|| MENU ||***\n");
  printf("\t[1] List\n\t[2] Download\n\t[3] Upload\n\t[4] Exit\n");
  fflush(stdout);
}

int file_select(struct dirent *entry){
  if((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)){
    return FALSE;
  }else{
    return TRUE;
  }
}

// funzione per salvare i timer e le informazioni dei pacchetti da ritrasmettere
void append_timer(timer_t idT, char *seq_num, t_tmr **list_head_ptr_timers,char *synbit,char* ackbit,char *acknum,char*msg,char* op_no,char* finbit,char* port, char *size){

  t_tmr *new_node = malloc(sizeof(t_tmr));
  
  if (new_node == NULL){
    printf("malloc error!\n");
    exit(1);
  }

  
  new_node->idTimer = idT;
  strcpy(new_node->sequence_number, seq_num);
  strcpy(new_node->SYNbit,synbit);
  strcpy(new_node->ACKbit,ackbit);
  strcpy(new_node->ack_number,acknum);
  memcpy(new_node->message, msg, MAXLINE);
  strcpy(new_node->operation_no,op_no);
  strcpy(new_node->FINbit, finbit);
  strcpy(new_node->port, port);
  strcpy(new_node->size, size);

  new_node->next = NULL;
  new_node->prev = NULL;

  
  /* se la lista è vuota append in testa*/
  if (*list_head_ptr_timers == NULL){
     
     *list_head_ptr_timers = new_node;
     list_head_tmr = *list_head_ptr_timers;
     return;
  }

  t_tmr *n = *list_head_ptr_timers;
  /* scorro la lista finché non trovo che il next del nodo è NULL per inserire in coda*/
  while (n->next != NULL){
      n=n->next;  
  }

  n->next = new_node;
  new_node->prev = n;
 
}

// funzione per salvare i timer e le informazioni dei pacchetti da ritrasmettere (cado adattivo)
#ifdef ADPTO
void append_timer_adaptive(timer_t idT, char *seq_num, t_tmr **list_head_ptr_timers,char *synbit,char* ackbit,char *acknum,char*msg,char* op_no,char* finbit,char* port, char *size, float rto_at, float last_rto_at, float sample_value_at, int retr){

  t_tmr *new_node = malloc(sizeof(t_tmr));
  
  if (new_node == NULL){
    printf("malloc error!\n");
    exit(1);
  }

  
  new_node->idTimer = idT;
  strcpy(new_node->sequence_number, seq_num);
  strcpy(new_node->SYNbit,synbit);
  strcpy(new_node->ACKbit,ackbit);
  strcpy(new_node->ack_number,acknum);
  memcpy(new_node->message, msg, MAXLINE);
  strcpy(new_node->operation_no,op_no);
  strcpy(new_node->FINbit, finbit);
  strcpy(new_node->port, port);
  strcpy(new_node->size, size);


  new_node->last_rto = rto_at; 
  if(retr == 0){
    new_node->sample_value = 0.0;
  }
  else{
    new_node->sample_value = sample_value_at + last_rto_at;
  }

  new_node->next = NULL;
  new_node->prev = NULL;

  
  /* se la lista è vuota append in testa*/
  if (*list_head_ptr_timers == NULL){
     
     *list_head_ptr_timers = new_node;
     list_head_tmr = *list_head_ptr_timers;
     return;
  }

  t_tmr *n = *list_head_ptr_timers;
  /* scorro la lista finché non trovo che il next del nodo è NULL per inserire in coda*/
  while (n->next != NULL){
      n=n->next;  
  }

  n->next = new_node;
  new_node->prev = n;
  
}
#endif

void print_list_tmr(t_tmr *list_head_ptr_tmrs){

   t_tmr *n;

   printf("\nLista collegata timers: ");
   for (n=list_head_ptr_tmrs;n!=NULL;n=n->next){
    // printf("\nTIMER: %p)", n->idTimer);
     fflush(stdout);
   }
}

// funzione per bufferizzare i pacchetti ricevuti fuori sequenza 
void append_buffered_packet(char *seq_num, char *acknum, char *message, char* op_no, t_buffered_pkt **list_head_ptr_buffered_pkts){

  t_buffered_pkt *new_node = malloc(sizeof(t_buffered_pkt));
  t_buffered_pkt *current;
  t_buffered_pkt *previous;

  current = *list_head_ptr_buffered_pkts;
  
  if (new_node == NULL){
    printf("malloc error!\n");
    exit(1);
  }

  strcpy(new_node->sequence_number, seq_num);
  strcpy(new_node->ack_number,acknum);
  memcpy(new_node->message, message, MAXLINE);
  strcpy(new_node->operation_no,op_no);
  new_node->next = NULL;
  new_node->prev = NULL;

  
  /* se la lista è vuota append in testa*/
  if (*list_head_ptr_buffered_pkts == NULL){
     *list_head_ptr_buffered_pkts = new_node;
     list_head_buffered_pkts = *list_head_ptr_buffered_pkts;
     return;
  }


  while (current != NULL){
      if((strncmp(message, current->message, MAXLINE) == 0) && (atoi(seq_num) == atoi(current->sequence_number))){
         // scarto il pacchetto perché è un duplicato
         receive_window++;
         return;
      }     
      
      previous = current;
      current = current->next;
  
  }

  previous->next = new_node;
  new_node->next = current;
  
}

// funzione per memorizzare le informazioni dei pacchetti da tritrasmettere in caso di ricezione di tre ack duplicati
void append_sent_packets(char *seq_num ,char *synbit, char* ackbit, char *acknum, char*msg, char* op_no, char* finbit, char *port, t_sent_pkt **list_head_ptr_sent_pkts, char *size){
  
    t_sent_pkt *new_node = malloc(sizeof(t_sent_pkt));
  
    if (new_node == NULL){
      printf("malloc error!\n");
      exit(1);
    }

    strcpy(new_node->sequence_number, seq_num);
    strcpy(new_node->SYNbit,synbit);
    strcpy(new_node->ACKbit,ackbit);
    strcpy(new_node->ack_number,acknum);
    memcpy(new_node->message, msg, MAXLINE);
    strcpy(new_node->operation_no,op_no);
    strcpy(new_node->FINbit, finbit);
    strcpy(new_node->port, port);
    strcpy(new_node->size, size);
    new_node->next = NULL;
    new_node->prev = NULL;

    
    /* se la lista è vuota append in testa*/
    if (*list_head_ptr_sent_pkts == NULL){
       *list_head_ptr_sent_pkts = new_node;
       list_head_sent_pkts = *list_head_ptr_sent_pkts;
       return;
    }

    t_sent_pkt *n = *list_head_ptr_sent_pkts;
    /* scorro la lista finché non trovo che il next del nodo è NULL per inserire in coda*/
    while (n->next != NULL){
        n=n->next;  
    }

    n->next = new_node;
    new_node->prev = n;
    
}


t_timid* append_id_timer(int master_enabler, t_timid **list_head_ptr_id_timers){

  t_timid *new_node = malloc(sizeof(t_timid));
  
  if (new_node == NULL){
    printf("malloc error!\n");
    exit(1);
  }

  
  new_node->id = (timer_t)timerid;
  new_node->master_enabler = master_enabler;
  new_node->next = NULL;
  new_node->prev = NULL;

  
  /* se la lista è vuota append in testa*/
  if (*list_head_ptr_id_timers == NULL){
     *list_head_ptr_id_timers = new_node;
     list_head_id_tmr = *list_head_ptr_id_timers;
     return new_node;
  }

  t_timid *n = *list_head_ptr_id_timers;
  /* scorro la lista finché non trovo che il next del nodo è NULL per inserire in coda*/
  while (n->next != NULL){
      n=n->next;  
  }

  n->next = new_node;
  new_node->prev = n;
  
  return new_node;

}

void append_real_idTimer(timer_t tidp, t_timid **list_head_ptr_id_timers){

  t_timid *previous;
  t_timid *current = *list_head_ptr_id_timers;

  if((current->id == timerid)){
      current->tidp = tidp;
      return;
    }

  while (current != NULL){
    if((current->id == timerid)){
      current->tidp = tidp;
      break;
    }
    else{
      previous = current;
      current = current->next;
    }      
  }
}

// funzione per deallocare la memoria usata per memorizzare le informazioni dei pacchetti ricevuti fuori sequnza e dunque bufferizzati
void delete_buffered_packet(char *seq_num, t_buffered_pkt **list_head_ptr_buffered_pkts){

   t_buffered_pkt *previous;
   t_buffered_pkt *tmp;
   t_buffered_pkt *current = *list_head_ptr_buffered_pkts;
    
   if(atoi(current->sequence_number) == atoi(seq_num)){
    //eliminazione in testa
    tmp = *list_head_ptr_buffered_pkts;
    *list_head_ptr_buffered_pkts = tmp->next;
    list_head_buffered_pkts = *list_head_ptr_buffered_pkts;
    
    free(tmp);
    return;
   }

   while (current != NULL){
      if(atoi(current->sequence_number) ==  atoi(seq_num)){
        //elemento trovato
        break;
      }
      else{
        previous = current;
        current = current->next;
      } 
       
   }

   //ora current è l'elemento trovato

   // eliminazione in lista
   if (current != NULL){
     tmp = current;
     previous->next = current->next;
     
     free(tmp);
     return;
   }
}

// funzione per deallocare l'area di memoria usata per la sival_ptr
void delete_id_timer(timer_t tidp, t_timid **list_head_ptr_id_tmrs){

   t_timid *previous;
   t_timid *tmp_timer = NULL;
   t_timid *current = *list_head_ptr_id_tmrs;

   //print_list_tmr(*list_head_ptr_id_tmrs);
    
   if(current->tidp == tidp){
      //eliminazione in testa
      tmp_timer = current;
      *list_head_ptr_id_tmrs = tmp_timer->next;
      list_head_id_tmr = *list_head_ptr_id_tmrs;
      free(tmp_timer);
      return;
   }

   while (current != NULL){
      if(current->tidp == tidp){
        //elemento trovato
        break;
      }
      else{
        previous = current;
        current = current->next;
      }   
   }

   //ora current è l'elemento trovato

   // eliminazione in lista
   if (current != NULL){
     tmp_timer = current;
     previous->next = current->next;
     free(tmp_timer);
     return;
   }
}



void print_list_timer(t_tmr *list_head_ptr){

   t_tmr *n;

   printf("\nLista collegata: ");
   for (n=list_head_ptr;n!=NULL;n=n->next){
      printf("SEQ_NUM: %s",n->sequence_number);
   }
}



void print_list_id_timer(t_timid *list_head_id_tmr){

   t_timid *n;

   printf("\nLista collegata: ");
   for (n=list_head_id_tmr;n!=NULL;n=n->next){
      printf("\nTimer: --> SOCKET: %d", n->sockfd);
   }
}

void print_list_sent_pkt(t_sent_pkt *list_head_ptr_sent_pkts){

   t_sent_pkt *n;

   printf("\nLista collegata sent pkt: ");
   for (n=list_head_ptr_sent_pkts;n!=NULL;n=n->next){
     printf("\nPACK: %s - SOCKET: %d)", n->sequence_number, n->sockfd);
     fflush(stdout);
   }
}

// funzione per eliminare il timer creato nel momento della
void delete_timer(timer_t idT, t_tmr **list_head_ptr_tmrs){

   t_tmr *previous;
   t_tmr *tmp;
   t_tmr *current = *list_head_ptr_tmrs;
  
   if(current->idTimer == idT){
    //eliminazione in testa
    tmp = *list_head_ptr_tmrs;
    *list_head_ptr_tmrs = tmp->next;
    list_head_tmr = *list_head_ptr_tmrs;
    
    free(tmp);
    return;
   }

   while (current != NULL){
      if(current->idTimer == idT){
        //elemento trovato
        break;
      }
      else{
        previous = current;
        current = current->next;
      } 
       
   }

   //ora current è l'elemento trovato

   // eliminazione in lista
   if (current != NULL){
     tmp = current;
     previous->next = current->next;
     
     free(tmp);
     return;
   }else{
     return;
   }


}

// funzione per deallocare lo spazio di memoria dedicato ai pacchetti memorizzati per la ritrasmissione in caso di tre ack duplicati
void delete_sent_packet(char *seq_num, t_sent_pkt **list_head_ptr_sent_pkts){

   t_sent_pkt *previous;
   t_sent_pkt *tmp_sent_pkt = NULL;
   t_sent_pkt *current = *list_head_ptr_sent_pkts;

   //print_list_sent_pkt(*list_head_ptr_sent_pkts);
   
   if((atoi(current->sequence_number) == atoi(seq_num))){
      //eliminazione in testa
      tmp_sent_pkt = current;
      *list_head_ptr_sent_pkts = tmp_sent_pkt->next;
      list_head_sent_pkts = *list_head_ptr_sent_pkts;
      free(tmp_sent_pkt);
      return;
   }

   while (current != NULL){
        if((atoi(current->sequence_number) == atoi(seq_num))){
          //elemento trovato
          break;
        }
        else{
          previous = current;
          current = current->next;
        } 
       
   }

   //ora current è l'elemento trovato

   // eliminazione in lista
   if (current != NULL){
     tmp_sent_pkt = current;
     previous->next = current->next;
     
     free(tmp_sent_pkt);
     return;
   }
}

float float_rand( float min, float max )
{
    float scale = rand() / (float) RAND_MAX; /* [0, 1.0] */
    return min + scale * ( max - min );      /* [min, max] */
}

static timer_t makeTimer()
{
    struct sigevent    te;
    struct itimerspec  its;
    timerid++;  
    int ret;
    int master_enabler = 0;
#ifdef ADPTO
    float rto_sec;
    float rto_nsec;
    float dec_part;
    
    dec_part = modff(rto, &rto_sec);
    rto_nsec = (rto - rto_sec)*pow(10,9);
#endif    
    // devo fare retrieve valore master timer
    
    if(master_timer == 1){    
       master_enabler = 1;
    }
    
    
    /* Set and enable alarm */
    te.sigev_notify = SIGEV_SIGNAL;
    te.sigev_signo = sigNo;
    te.sigev_value.sival_ptr = append_id_timer(master_enabler, &list_head_id_tmr);

    //così facendo si spera che sival punta sempre ad area di memoria differente
    timer_t tidp;
    
    if(timer_create(CLOCK_REALTIME,&te, &tidp) != 0){
       perror("\n timer_create ERROR");
    }

    append_real_idTimer(tidp, &list_head_id_tmr);


    if(master_timer == 1){
      //imposto timer lungo di controllo
      master_IDTimer = tidp;
      its.it_interval.tv_sec = 0;
      its.it_interval.tv_nsec = 0;
      its.it_value.tv_sec = 1000;
      its.it_value.tv_nsec = 0;
    }
    else if(flag_last_ack_sent == 1){
      its.it_interval.tv_sec = 0;
      its.it_interval.tv_nsec = 0;
      its.it_value.tv_sec = 4; //mi copro di 5 tentativi di invio nel caso si perda il mio ultimo ack verso il server
      its.it_value.tv_nsec = 0;
    }
    else{
      its.it_interval.tv_sec = 0;
      its.it_interval.tv_nsec = 0;
#ifdef ADPTO
      its.it_value.tv_sec = rto_sec;
      its.it_value.tv_nsec = rto_nsec;
#else
      its.it_value.tv_sec = 0.0;
      its.it_value.tv_nsec = 0.05*pow(10,9);
#endif
    }


    ret = timer_settime(tidp, 0, &its, NULL);
    if(ret != 0){
      perror("Errore in settime server\n");
      fflush(stdout);
      exit(1);
    }
    //timer started

    return tidp;
}


static timer_t srtSchedule(){
    timer_t rc;
    rc = makeTimer();
    return rc;
}

void sig_usr_handler(){

   return;
}


// funzione per gestire il ctrl+C da parte del client
void handler(){

  end_hs = 0; //mi metto in ascolto sul main thread del server
  float loss_p;
  timer_t ret_timer;
  struct sembuf oper_timer;

  ctrlc_signal_flag = 1;

  t_data *new_data = malloc(sizeof(t_data));
  if(new_data == NULL){
    perror("malloc error");
    exit(1);
  }

  //srand(time(NULL));
  num = rand()%10000;
  snprintf(new_data->sequence_number, sizeof(new_data->sequence_number), "%d", num);
  strcpy(new_data->FINbit,"1\0");
  switch_fin = 1;


  oper_timer.sem_num = 0;
  oper_timer.sem_op = -1;
  oper_timer.sem_flg = 0;

rewait_ctrlc:
  if((semop(semid_timer,&oper_timer,1)) == -1){
     perror("Error waiting ctrlc\n");
     if(errno == EINTR){
      goto rewait_ctrlc;
     }
     exit(-1);
  }
  
  loss_p = float_rand(0.0,1.0);
  
  if(loss_p >= LOSS_PROBABILITY){
    if((sendto(sockfd, new_data, 1500, 0, (struct sockaddr*)&servaddr, (socklen_t)sizeof(servaddr))) < 0){
      perror("Errore in sendto 1\n");
      exit(1);
    }
  }
  

  ret_timer = srtSchedule();
#ifdef ADPTO
  append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr, new_data->SYNbit, new_data->ACKbit, new_data->ack_number, new_data->message, new_data->operation_no, new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
  append_timer(ret_timer, new_data->sequence_number, &list_head_tmr, new_data->SYNbit, new_data->ACKbit, new_data->ack_number, new_data->message, new_data->operation_no, new_data->FINbit,new_data->port, new_data->size);
#endif


  oper_timer.sem_num = 0;
  oper_timer.sem_op = 1;
  oper_timer.sem_flg = 0;

resignal_ctrlc:
  if((semop(semid_timer,&oper_timer,1)) == -1){
     perror("Error resignal_ctrlc\n");
     if(errno == EINTR){
      goto resignal_ctrlc;
     }
     exit(-1);
  }

  client_closing = 1;
  free(new_data);
  printf("\nSignal sent to server\n");
  fflush(stdout);

  
}

// funzione per gestire il caso in cui il client richieda la chiusura della connessione tramite il comando 4 del menu
void quit(){

  end_hs = 0; //mi metto in ascolto sul main thread del server
  first_msg_close = 1;
  float loss_p;
  timer_t ret_timer;
  struct sembuf oper_timer;

  t_data *new_data = malloc(sizeof(t_data));
  if(new_data == NULL){
    perror("malloc error");
    exit(1);
  }

  //srand(time(NULL));
  num = rand()%10000;
  snprintf(new_data->sequence_number,sizeof(new_data->sequence_number),"%d",num);
  snprintf(new_data->ack_number,sizeof(new_data->ack_number),"%d",a_num);
  strcpy(new_data->FINbit,"1\0");
  strcpy(new_data->ACKbit,"0\0");
  switch_fin = 1;


  oper_timer.sem_num = 0;
  oper_timer.sem_op = -1;
  oper_timer.sem_flg = 0;

reclose:
  if((semop(semid_timer,&oper_timer,1)) == -1){
     perror("Error signaling close\n");
     if(errno == EINTR){
      goto reclose;
     }
     exit(-1);
  }
  
  loss_p = float_rand(0.0,1.0);
  
  if(loss_p >= LOSS_PROBABILITY){
    if((sendto(sockfd, new_data, 1500, 0, (struct sockaddr*)&servaddr, (socklen_t)sizeof(servaddr))) < 0){
      perror("Errore sendto 2\n");
      exit(1);
    } 
  }
  

  ret_timer = srtSchedule();
#ifdef ADPTO
  append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr, new_data->SYNbit, new_data->ACKbit, new_data->ack_number, new_data->message, new_data->operation_no, new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
  append_timer(ret_timer, new_data->sequence_number, &list_head_tmr, new_data->SYNbit, new_data->ACKbit, new_data->ack_number, new_data->message, new_data->operation_no, new_data->FINbit,new_data->port, new_data->size);
#endif


  oper_timer.sem_num = 0;
  oper_timer.sem_op = 1;
  oper_timer.sem_flg = 0;

reclose2:
  if((semop(semid_timer,&oper_timer,1)) == -1){
     perror("Error close 2\n");
     if(errno == EINTR){
      goto reclose2;
     }
     exit(-1);
  }

  client_closing = 1;
  free(new_data);
  printf("\nClient quitting...\n");
  fflush(stdout);
}

// funzione per gestire la ritrasmissione in caso di ricezione di tre ack duplicati
void three_duplicate_message(char *ack_num){

  t_sent_pkt *n;
  t_tmr *del_timer;
  struct sembuf oper_recv,oper_turn;
  timer_t ret_timer;
  float loss_p;
  int ret;

  retr_phase = 1;

  oper_turn.sem_num = 0;
  oper_turn.sem_op = -1;
  oper_turn.sem_flg = 0;


resignaling1:      
  //wait sul valore timer
  if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
      perror("Error signaling 1\n");
      if(errno == EINTR){
        goto resignaling1;
      }
      
      exit(-1);
  }

  for(n = list_head_sent_pkts; n != NULL; n = n->next){
    if(atoi(n->sequence_number) == atoi(ack_num)) {
      // ultimo ack correttamente inviato trovato!

      t_data *new_data = malloc(sizeof(t_data));
      if(new_data == NULL){
        perror("malloc error");
        exit(-1);
      }

      // popolo new_data->sequence
      strcpy(new_data->sequence_number, n->sequence_number);

      // popolo il new_data->operation_no
      strcpy(new_data->operation_no, n->operation_no);

      // popolo il campo new_data->ack_number
      strcpy(new_data->ack_number, n->ack_number);
    
      // popolo il new_data->message
      memcpy(new_data->message, n->message, MAXLINE);
      
      // popola la porta verso la quale ritrametto
      strcpy(new_data->port, n->port);

      for(del_timer = list_head_tmr; del_timer != NULL; del_timer = del_timer->next){
        if((atoi(del_timer->sequence_number) == atoi(ack_num))){

          ret_timer = srtSchedule();
#ifdef ADPTO
          append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr, new_data->SYNbit, new_data->ACKbit, new_data->ack_number, new_data->message, new_data->operation_no, new_data->FINbit,new_data->port, new_data->size, rto, del_timer->last_rto, del_timer->sample_value , 1);
#else
          append_timer(ret_timer, new_data->sequence_number, &list_head_tmr, new_data->SYNbit, new_data->ACKbit, new_data->ack_number, new_data->message, new_data->operation_no, new_data->FINbit,new_data->port, new_data->size);
#endif
          
          ret = timer_delete(del_timer->idTimer);
          if(ret == -1){
            perror("\nErrore in cancellazione ret_timer\n");
            exit(1);
          }

          delete_id_timer(del_timer->idTimer, &list_head_id_tmr);
          delete_timer(del_timer->idTimer, &list_head_tmr);

          break;
        }
      }

      // sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
      loss_p = float_rand(0.0, 1.0);
      
      if(loss_p >= LOSS_PROBABILITY){
          if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
             perror("\n(1)errore in sendto retransmission");
             exit(1);
          }   
      }
     
      
      oper_recv.sem_num = 0;
      oper_recv.sem_op = 1;
      oper_recv.sem_flg = 0;

signal2:
      // signal
      if((semop(semid2, &oper_recv, 1)) == -1){
        perror("Error signal2\n");
        if(errno == EINTR){
          goto signal2;
        }
        exit(-1);
      }   

    
      oper_turn.sem_num = 0;
      oper_turn.sem_op = 1;
      oper_turn.sem_flg = 0;


signal41:
      //signal sul turno di retransmit
      if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
         perror("Error signaling 41\n");
         if(errno == EINTR){
          goto signal41;
         }
         exit(-1);
      }

      free(new_data);
      return;
    }
  }



  oper_turn.sem_num = 0;
  oper_turn.sem_op = 1;
  oper_turn.sem_flg = 0;

resignaling3:      
  //wait sul valore timer
  if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
      perror("Error signaling 3\n");
      if(errno == EINTR){
        goto resignaling3;
      }
      
      exit(-1);
  }
}




void handling_master_timer(timer_t tidp, t_tmr** list_head_global_tmr){
  
    t_tmr* n;
    timer_t ret_timer;
    int ret;
    float loss_p;
    struct sembuf oper_timer;

    //wait sul timer 
    oper_timer.sem_num = 0;
    oper_timer.sem_op = -1;
    oper_timer.sem_flg = 0;

rewait3:
    if((semop(semid_timer, &oper_timer, 1)) == -1){
      perror("Error waiting 3\n");
      if(errno == EINTR){
        goto rewait3;
      }
      exit(-1);
    }

    for(n = *list_head_global_tmr; n != NULL; n = n->next){
      //individuo i pacchetti che devo ritramettere verso il client dest_client
    
        // retransmit del pacchetto n verso il client dest_client
        t_data *new_data = malloc(sizeof(t_data));
        if(new_data == NULL){
            perror("malloc error");
            exit(1);
        }

        // popolo new_data->sequence
        strcpy(new_data->sequence_number,n->sequence_number);

        // popolo il new_data->operation_no
        strcpy(new_data->operation_no, n->operation_no);

        // popolo il campo new_data->ack_number
        strcpy(new_data->ack_number, n->ack_number);
          
        // popolo il new_data->message
        memcpy(new_data->message, n->message, MAXLINE);
          
        // popola la porta verso la quale ritrametto
        strcpy(new_data->port, n->port);

        ret = timer_delete(n->idTimer);
        if(ret == -1){
          perror("\nErrore in cancellazione ret_timer\n");
          exit(1);
        }

        delete_id_timer(n->idTimer, &list_head_id_tmr);
        delete_timer(n->idTimer, &list_head_tmr);
        
        ret_timer = srtSchedule();
#ifdef ADPTO
        append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
        append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);
#endif
        
        // sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
        loss_p = float_rand(0.0, 1.0);
        if(loss_p >= LOSS_PROBABILITY){
            if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
               perror("\n(1)errore in sendto retransmission");
               exit(1);
            }   
        }
        
        free(new_data);

    }
      

    ret = timer_delete(tidp);
    if(ret == -1){
      perror("\nErrore in cancellazione ret_timer\n");
      exit(1);
    }

      //signal sul timer 
    oper_timer.sem_num = 0;
    oper_timer.sem_op = 1;
    oper_timer.sem_flg = 0;

signal4:
    if((semop(semid_timer, &oper_timer, 1)) == -1){
      perror("Error signal4\n");
      if(errno == EINTR){
        goto signal4;
      }
      exit(-1);
    }
      

}

// thread incaricato di andare ad esguire la ritrasmissione di un pacchetto in caso di scandenza di un timer
static void *signal_handler_thread(){

    t_timid *tmr;
    siginfo_t info;
    sigset_t set;
    timer_t ret_timer;
    int ret;
    struct sembuf oper, oper2, oper_timer, oper_turn;
    float loss_p;
    t_tmr *t;
    int find = 0;
    int signum;

    sigemptyset(&set);
    sigaddset(&set, SIGRTMIN);

    while(1){

        signum = sigwaitinfo(&set, &info);
        if(signum == -1){
           continue;
        }

        tmr = (&info)->si_value.sival_ptr;
        
        if(end_hs == 0 || closing_ack == 1){
           kill(getpid(), SIGUSR1);
           if(flag_close_wait == 1){
              continue;
           }
        }

       
        if((tmr)->master_enabler == 1){
          handling_master_timer((tmr)->tidp, &list_head_tmr);
          continue;
        }


        oper_timer.sem_num = 0;
        oper_timer.sem_op = -1;
        oper_timer.sem_flg = 0;

wait78:
        if((semop(semid_timer,&oper_timer,1)) == -1){
           perror("Error wait 78\n");
           if(errno == EINTR){
            goto wait78;
           }
           exit(-1);
        }

        oper_turn.sem_num = 0;
        oper_turn.sem_op = -1;
        oper_turn.sem_flg = 0;

wait77:
        if((semop(semid_retr_turn,&oper_turn,1)) == -1){
           perror("Error wait 77\n");
           if(errno == EINTR){
            goto wait77;
           }
           exit(-1);
        }

        t = NULL;
        find = 0;

        //controllo se il pkt è stato eliminato da un ack cumulativo
        for(t = list_head_tmr; t != NULL; t = t->next){
          if(t->idTimer == tmr->tidp){
            if(t->sequence_number < last_ack){
              break;
            }
            find = 1;
            break;

          }

        }


        if(flag_final_timer == 1 || flag_close_wait == 1){

          oper2.sem_num = 0;
          oper2.sem_op = 1;
          oper2.sem_flg = 0;

rewait_final_t1:
          //signal fine hs
          if((semop(semid_handshake, &oper2, 1)) == -1){
             perror("Error waiting hst\n");
             if(errno == EINTR){
              goto rewait_final_t1;
             }
             exit(-1);
          }
          continue;
        }

        
        if(find == 0 || flag_last_hs == 1){
          
          
          if(end_hs == 0){

            delete_id_timer(t->idTimer, &list_head_id_tmr);
            delete_timer(t->idTimer, &list_head_tmr);
            flag_last_hs = 0;


            oper2.sem_num = 0;
            oper2.sem_op = 1;
            oper2.sem_flg = 0;

  resignal_hst:
            //signal fine hs
            if((semop(semid_handshake, &oper2, 1)) == -1){
               perror("Error waiting hst\n");
               if(errno == EINTR){
                goto resignal_hst;
               }
               exit(-1);
            }
          }

          
          
          oper_turn.sem_num = 0;
          oper_turn.sem_op = 1;
          oper_turn.sem_flg = 0;

  signal5:
          //signal sul turno di retransmit
          if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
             perror("Error signaling 6\n");
             if(errno == EINTR){
              goto signal5;
             }
             exit(-1);
          }

          // pacchetto da ritrasmettere non trovato --> pthread_exit()
          oper_timer.sem_num = 0;
          oper_timer.sem_op = 1;
          oper_timer.sem_flg = 0;
    
  signal6:        
          if((semop(semid_timer, &oper_timer, 1)) == -1){
             perror("Error signal 6\n");
             if(errno == EINTR){
              goto signal6;
             }
             exit(-1);
          }

          
          find = 0;

          continue;

        }

        if(closing_ack == 1){
          
          ret = timer_delete(list_head_tmr->idTimer);
          if(ret == -1){
            perror("\ntimer_delete error");
            exit(-1);
          }
          
          delete_id_timer(t->idTimer, &list_head_id_tmr);
          delete_timer(t->idTimer, &list_head_tmr);

          //signal sul semaforo ultimo ack
          oper_turn.sem_num = 0;
          oper_turn.sem_op = 1;
          oper_turn.sem_flg = 0;

  signal7:
          //signal sul turno di retransmit
          if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
             perror("Error signaling 7\n");
             if(errno == EINTR){
              goto signal7;
             }
             exit(-1);
          }


          // pacchetto da ritrasmettere non trovato --> pthread_exit()
          oper_timer.sem_num = 0;
          oper_timer.sem_op = 1;
          oper_timer.sem_flg = 0;
   
  signal8:         
          if((semop(semid_timer, &oper_timer, 1)) == -1){
             perror("Error signal 8\n");
             if(errno == EINTR){
              goto signal8;
             }
             exit(-1);
          }

          oper.sem_num = 0;
          oper.sem_op = 1;
          oper.sem_flg = 0;

  signal9:
          if((semop(semid3,&oper,1)) == -1){
             perror("Error signaling 1\n");
             if(errno == EINTR){
              goto signal9;
             }
             exit(-1);
          }
         
          continue;

        }

        if(name_download == 1){
          servaddr1.sin_port = htons(porta_send_download_file);
        }

        if((int)strlen(t->sequence_number) == 0){
          continue;
        }
        // retransmit del pacchetto n
        t_data *new_data = malloc(sizeof(t_data));
        if(new_data == NULL){
            perror("malloc error");
            exit(1);
        }

        
        // popolo new_data->sequence
        strcpy(new_data->sequence_number,t->sequence_number);

        // popolo il new_dat->operation_no
        strcpy(new_data->operation_no, t->operation_no);

        // popolo il new_data->message
        memcpy(new_data->message, t->message, MAXLINE);

        //questo campo viene popolato solo nel caso dell'upload, altrimenti dovrebbe rimanere vuoto senza dare problemi
        strcpy(new_data->port, t->port);

        if(flag_size_upload == 1){
          strcpy(new_data->size, t->size);
        }


        if(end_hs == 0){
          if(client_closing == 1 || flag_close_wait == 1){
            strcpy(new_data->FINbit,t->FINbit);
          }else{
            strcpy(new_data->SYNbit,t->SYNbit);
          }
          strcpy(new_data->ACKbit,t->ACKbit);
        }

        ret_timer = srtSchedule();
  #ifdef ADPTO
        append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, t->last_rto, t->sample_value, 1);
  #else
        append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);
  #endif
        
        ret = timer_delete(t->idTimer);
        if(ret == -1){
          perror("\ntimer_delete error");
          exit(-1);
        }
        delete_id_timer(t->idTimer, &list_head_id_tmr);
        delete_timer(t->idTimer, &list_head_tmr);
        
        loss_p = float_rand(0.0, 1.0);
        
        if(strcmp(new_data->message, def_str) == 0 || end_hs == 0){
            if(loss_p >= LOSS_PROBABILITY){

                // sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
                if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                    perror("\nerrore in sendto 5");
                    exit(1);
                }       
            }

             
        }
        else{
            if(loss_p >= LOSS_PROBABILITY){
                // sto ritrasmettendo un messaggio verso uno dei thread figli del server main thread --> uso SERVADDR1
                if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                    perror("\nerrore in sendto 6");
                    exit(1);
                }       
            }

            
        }  
        

        free(new_data);

         // pacchetto da ritrasmettere non trovato --> pthread_exit()

        //queste due signal non le faccio con handshake perchè in hs non ho bisogno di questi due semafori
        if(end_hs == 1){
          

          oper2.sem_num = 0;
          oper2.sem_op = 1;
          oper2.sem_flg = 0;

  signal11:
          //signal sul turno di retransmit
          if((semop(semid2, &oper2, 1)) == -1){
             perror("Error signaling 6\n");
             if(errno == EINTR){
              goto signal11;
             }
             exit(-1);
          }

        }else{

          if(client_closing == 0){

            oper2.sem_num = 0;
            oper2.sem_op = 1;
            oper2.sem_flg = 0;

  rewait_hst:
            //signal fine hs
            if((semop(semid_handshake, &oper2, 1)) == -1){
               perror("Error waiting hst\n");
               if(errno == EINTR){
                goto rewait_hst;
               }
               exit(-1);
            }
          }

        }
        

        oper_turn.sem_num = 0;
        oper_turn.sem_op = 1;
        oper_turn.sem_flg = 0;

  signal12:
        //signal sul turno di retransmit
        if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
           perror("Error signaling 12\n");
           if(errno == EINTR){
                goto signal12;
            }
           exit(-1);
        }

        oper_timer.sem_num = 0;
        oper_timer.sem_op = 1;
        oper_timer.sem_flg = 0;
   
  signal13:       
        if((semop(semid_timer, &oper_timer, 1)) == -1){
           perror("Error signal13\n");
           if(errno == EINTR){
                goto signal13;
           }
           exit(-1);
        }

      

        continue;


    }
}

void *handling_timeout_retransmission_th(void *pack){

      timer_t ret_timer;
      int ret;
      t_tmr *n = (t_tmr *)pack;
      int ret_val = 666;
      struct sembuf oper, oper2, oper_timer, oper_turn;
      float loss_p;
      t_tmr *t;
      int find = 0;
      sigset_t  set;

      sigfillset(&set);
      if(pthread_sigmask(SIG_SETMASK, &set, NULL) != 0){
        perror("\npthread_sigmask error");
        exit(-1);
      }

      retr_phase = 1;

      oper_timer.sem_num = 0;
      oper_timer.sem_op = -1;
      oper_timer.sem_flg = 0;

wait78:
      if((semop(semid_timer,&oper_timer,1)) == -1){
         perror("Error wait 78\n");
         if(errno == EINTR){
          goto wait78;
         }
         exit(-1);
      }

      oper_turn.sem_num = 0;
      oper_turn.sem_op = -1;
      oper_turn.sem_flg = 0;

wait77:
      if((semop(semid_retr_turn,&oper_turn,1)) == -1){
         perror("Error wait 77\n");
         if(errno == EINTR){
          goto wait77;
         }
         exit(-1);
      }

      //controllo se il pkt è stato eliminato da un ack cumulativo
      for(t = list_head_tmr; t != NULL; t = t->next){
        if(t->idTimer == n->idTimer){
          if(t->sequence_number < last_ack){
            break;
          }
          find = 1;
          break;

        }

      }


      if(find == 0 || flag_last_hs == 1){
        
        if(end_hs == 0){

          delete_id_timer(n->idTimer, &list_head_id_tmr);
          delete_timer(n->idTimer, &list_head_tmr);
          flag_last_hs = 0;


          oper2.sem_num = 0;
          oper2.sem_op = 1;
          oper2.sem_flg = 0;

resignal_hst:
          //signal fine hs
          if((semop(semid_handshake, &oper2, 1)) == -1){
             perror("Error waiting hst\n");
             if(errno == EINTR){
              goto resignal_hst;
             }
             exit(-1);
          }
        }
        
        oper_turn.sem_num = 0;
        oper_turn.sem_op = 1;
        oper_turn.sem_flg = 0;

signal5:
        //signal sul turno di retransmit
        if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
           perror("Error signaling 6\n");
           if(errno == EINTR){
            goto signal5;
           }
           exit(-1);
        }

        // pacchetto da ritrasmettere non trovato --> pthread_exit()
        oper_timer.sem_num = 0;
        oper_timer.sem_op = 1;
        oper_timer.sem_flg = 0;
  
signal6:        
        if((semop(semid_timer, &oper_timer, 1)) == -1){
           perror("Error signal 6\n");
           if(errno == EINTR){
            goto signal6;
           }
           exit(-1);
        }

        pthread_exit((void *)&ret_val);
      }

      if(closing_ack == 1){
        ret = timer_delete(list_head_tmr->idTimer);
        if(ret == -1){
          perror("\ntimer_delete error");
          exit(-1);
        }
        
        delete_id_timer(n->idTimer, &list_head_id_tmr);
        delete_timer(n->idTimer, &list_head_tmr);

        //signal sul semaforo ultimo ack
        oper_turn.sem_num = 0;
        oper_turn.sem_op = 1;
        oper_turn.sem_flg = 0;

signal7:
        //signal sul turno di retransmit
        if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
           perror("Error signaling 7\n");
           if(errno == EINTR){
            goto signal7;
           }
           exit(-1);
        }

        // pacchetto da ritrasmettere non trovato --> pthread_exit()
        oper_timer.sem_num = 0;
        oper_timer.sem_op = 1;
        oper_timer.sem_flg = 0;
 
signal8:         
        if((semop(semid_timer, &oper_timer, 1)) == -1){
           perror("Error signal 8\n");
           if(errno == EINTR){
            goto signal8;
           }
           exit(-1);
        }

        oper.sem_num = 0;
        oper.sem_op = 1;
        oper.sem_flg = 0;

signal9:
        if((semop(semid3,&oper,1)) == -1){
           perror("Error signaling 1\n");
           if(errno == EINTR){
            goto signal9;
           }
           exit(-1);
        }

        pthread_exit((void *)&ret_val);
      }

      if(name_download == 1){
        servaddr1.sin_port = htons(porta_send_download_file);
      }

    
      // retransmit del pacchetto n
      t_data *new_data = malloc(sizeof(t_data));
      if(new_data == NULL){
          perror("malloc error");
          exit(1);
      }

      // popolo new_data->sequence
      strcpy(new_data->sequence_number,n->sequence_number);

      // popolo il new_dat->operation_no
      strcpy(new_data->operation_no, n->operation_no);

      // popolo il new_data->message
      memcpy(new_data->message, n->message, MAXLINE);

      //questo campo viene popolato solo nel caso dell'upload, altrimenti dovrebbe rimanere vuoto senza dare problemi
      strcpy(new_data->port, n->port);

      if(flag_size_upload == 1){
        strcpy(new_data->size, n->size);
      }


      if(end_hs == 0){
        if(client_closing == 1 || flag_close_wait == 1){
          strcpy(new_data->FINbit,n->FINbit);
        }else{
          strcpy(new_data->SYNbit,n->SYNbit);
        }
        strcpy(new_data->ACKbit,n->ACKbit);
      }

      ret_timer = srtSchedule();
#ifdef ADPTO
      append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, n->last_rto, n->sample_value, 1);
#else
      append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);
#endif
      
      delete_id_timer(n->idTimer, &list_head_id_tmr);
      delete_timer(n->idTimer, &list_head_tmr);
      
      loss_p = float_rand(0.0, 1.0);
      
      if(strcmp(new_data->message, def_str) == 0 || end_hs == 0){
          if(loss_p >= LOSS_PROBABILITY){
              // sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
              if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                  perror("\nerrore in sendto 5");
                  exit(1);
              }       
          }
           
      }
      else{
          if(loss_p >= LOSS_PROBABILITY){
              // sto ritrasmettendo un messaggio verso uno dei thread figli del server main thread --> uso SERVADDR1
              if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                  perror("\nerrore in sendto 6");
                  exit(1);
              }       
          }
          
      }  
      
      
      free(new_data);

       // pacchetto da ritrasmettere non trovato --> pthread_exit()

      //queste due signal non le faccio con handshake perchè in hs non ho bisogno di questi due semafori
      if(end_hs == 1){
        

        oper2.sem_num = 0;
        oper2.sem_op = 1;
        oper2.sem_flg = 0;

signal11:
        //signal sul turno di retransmit
        if((semop(semid2, &oper2, 1)) == -1){
           perror("Error signaling 6\n");
           if(errno == EINTR){
            goto signal11;
           }
           exit(-1);
        }

      }else{

        if(client_closing == 0){

          oper2.sem_num = 0;
          oper2.sem_op = 1;
          oper2.sem_flg = 0;

rewait_hst:
          //signal fine hs
          if((semop(semid_handshake, &oper2, 1)) == -1){
             perror("Error waiting hst\n");
             if(errno == EINTR){
              goto rewait_hst;
             }
             exit(-1);
          }
        }

      }
      

      oper_turn.sem_num = 0;
      oper_turn.sem_op = 1;
      oper_turn.sem_flg = 0;

signal12:
      //signal sul turno di retransmit
      if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
         perror("Error signaling 12\n");
         if(errno == EINTR){
              goto signal12;
          }
         exit(-1);
      }

      oper_timer.sem_num = 0;
      oper_timer.sem_op = 1;
      oper_timer.sem_flg = 0;
 
signal13:       
      if((semop(semid_timer, &oper_timer, 1)) == -1){
         perror("Error signal13\n");
         if(errno == EINTR){
              goto signal13;
         }
         exit(-1);
      }

      pthread_exit((void *)&ret_val);

      return NULL;
}


void handling_timeout_retransmission(t_tmr *n){

      timer_t ret_timer;
      int ret;
      struct sembuf oper, oper2, oper_timer, oper_turn;
      float loss_p;
      t_tmr *t;
      int find = 0;

      retr_phase = 1;

      oper_timer.sem_num = 0;
      oper_timer.sem_op = -1;
      oper_timer.sem_flg = 0;

signal133:
      if((semop(semid_timer,&oper_timer,1)) == -1){
         perror("Error signaling 13\n");
         if(errno == EINTR){
              goto signal133;
          }
         exit(-1);
      }

      oper_turn.sem_num = 0;
      oper_turn.sem_op = -1;
      oper_turn.sem_flg = 0;

signal15:
      if((semop(semid_retr_turn,&oper_turn,1)) == -1){
         perror("Error signaling 1\n");
         if(errno == EINTR){
          goto signal15;
         }
         exit(-1);
      }

      //controllo se il pkt è stato eliminato da un ack cumulativo
      for(t = list_head_tmr; t != NULL; t = t->next){
        if(t->idTimer == n->idTimer){
          if(t->sequence_number < last_ack){
            break;
          }
          find = 1;
          break;

        }

      }


      if(find == 0 || flag_last_hs == 1){
        
        oper_turn.sem_num = 0;
        oper_turn.sem_op = 1;
        oper_turn.sem_flg = 0;

signal13:
        //signal sul turno di retransmit
        if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
           perror("Error signaling 6\n");
           if(errno == EINTR){
              goto signal13;
           }
           exit(-1);
        }

        // pacchetto da ritrasmettere non trovato --> pthread_exit()
        oper_timer.sem_num = 0;
        oper_timer.sem_op = 1;
        oper_timer.sem_flg = 0;
 
signal14:         
        if((semop(semid_timer, &oper_timer, 1)) == -1){
           perror("Error waiting 4\n");
           if(errno == EINTR){
              goto signal14;
           }
           exit(-1);
        }

        if(end_hs == 0){

          delete_id_timer(n->idTimer, &list_head_id_tmr);
          delete_timer(n->idTimer, &list_head_tmr);
          flag_last_hs = 0;


          oper2.sem_num = 0;
          oper2.sem_op = 1;
          oper2.sem_flg = 0;

resignal_hst:
          //signal fine hs
          if((semop(semid_handshake, &oper2, 1)) == -1){
             perror("Error waiting hst\n");
             if(errno == EINTR){
              goto resignal_hst;
             }
             exit(-1);
          }
        }
        

        return;
      }

      if(closing_ack == 1){
        
        ret = timer_delete(list_head_tmr->idTimer);
        if(ret == -1){
          perror("\ntimer_delete error");
          exit(-1);
        }
        
        delete_id_timer(n->idTimer, &list_head_id_tmr);
        delete_timer(n->idTimer, &list_head_tmr);
        
        //signal sul semaforo ultimo ack
        oper_turn.sem_num = 0;
        oper_turn.sem_op = 1;
        oper_turn.sem_flg = 0;

signal16:
        //signal sul turno di retransmit
        if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
           perror("Error signaling 6\n");
           if(errno == EINTR){
            goto signal16;
           }
           exit(-1);
        }

        // pacchetto da ritrasmettere non trovato --> pthread_exit()
        oper_timer.sem_num = 0;
        oper_timer.sem_op = 1;
        oper_timer.sem_flg = 0;
          
signal17:
        if((semop(semid_timer, &oper_timer, 1)) == -1){
           perror("Error signal 17\n");
           if(errno == EINTR){
            goto signal17;
           }
           exit(-1);
        }

        oper.sem_num = 0;
        oper.sem_op = 1;
        oper.sem_flg = 0;

signal18:
        if((semop(semid3,&oper,1)) == -1){
           perror("Error signal 18\n");
           if(errno == EINTR){
            goto signal18;
           }
           exit(-1);
        }

        return;
      }

      if(name_download == 1){
        servaddr1.sin_port = htons(porta_send_download_file);
      }

    
      // retransmit del pacchetto n
      t_data *new_data = malloc(sizeof(t_data));
      if(new_data == NULL){
          perror("malloc error");
          exit(1);
      }

      // popolo new_data->sequence
      strcpy(new_data->sequence_number,n->sequence_number);

      // popolo il new_dat->operation_no
      strcpy(new_data->operation_no, n->operation_no);

      // popolo il new_data->message
      memcpy(new_data->message, n->message, MAXLINE);

      //questo campo viene popolato solo nel caso dell'upload, altrimenti dovrebbe rimanere vuoto senza dare problemi
      strcpy(new_data->port, n->port);

      if(flag_size_upload == 1){
        strcpy(new_data->size, n->size);
      }


      if(end_hs == 0){
        if(client_closing == 1 || flag_close_wait == 1){
          strcpy(new_data->FINbit,n->FINbit);
        }else{
          strcpy(new_data->SYNbit,n->SYNbit);
        }
        strcpy(new_data->ACKbit,n->ACKbit);
      }
      
      loss_p = float_rand(0.0, 1.0);
      
      if(strcmp(new_data->message, def_str) == 0 || end_hs == 0){
          if(loss_p >= LOSS_PROBABILITY){
              // sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
              if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                  perror("\nerrore in sendto 5");
                  exit(1);
              }      
          }
           
      }
      
      else{
          if(loss_p >= LOSS_PROBABILITY){
              // sto ritrasmettendo un messaggio verso uno dei thread figli del server main thread --> uso SERVADDR1
              if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                  perror("\nerrore in sendto 6");
                  exit(1);
              }      
          }
          
      }  
      
      ret_timer = srtSchedule();
#ifdef ADPTO
      append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, n->last_rto, n->sample_value, 1);
#else
      append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);
#endif
      
      delete_id_timer(n->idTimer, &list_head_id_tmr);
      delete_timer(n->idTimer, &list_head_tmr);
      
      free(new_data);

       // pacchetto da ritrasmettere non trovato --> pthread_exit()

      //queste due signal non le faccio con handshake perchè in hs non ho bisogno di questi due semafori
      if(end_hs == 1){
        

        oper2.sem_num = 0;
        oper2.sem_op = 1;
        oper2.sem_flg = 0;

signal20:
        //signal sul turno di retransmit
        if((semop(semid2, &oper2, 1)) == -1){
           perror("Error signaling 20\n");
           if(errno == EINTR){
            goto signal20;
           }
           exit(-1);
        }

      }else{

        if(client_closing == 0){

          oper2.sem_num = 0;
          oper2.sem_op = 1;
          oper2.sem_flg = 0;

rewait_hst:
          //signal fine hs
          if((semop(semid_handshake, &oper2, 1)) == -1){
             perror("Error waiting hst\n");
             if(errno == EINTR){
              goto rewait_hst;
             }
             exit(-1);
          }
        }

      }
      

      oper_turn.sem_num = 0;
      oper_turn.sem_op = 1;
      oper_turn.sem_flg = 0;

signal21:
      //signal sul turno di retransmit
      if((semop(semid_retr_turn, &oper_turn, 1)) == -1){
         perror("Error signaling 21\n");
         if(errno == EINTR){
          goto signal21;
         }
         exit(-1);
      }

      oper_timer.sem_num = 0;
      oper_timer.sem_op = 1;
      oper_timer.sem_flg = 0;
        
signal22:
      if((semop(semid_timer, &oper_timer, 1)) == -1){
         perror("Error waiting 4\n");
         if(errno == EINTR){
          goto signal22;
         }
         exit(-1);
      }
      
      
}

// thread incarito della gestione della ricezione degli ack durante l'operazione di upload
void *ack_upload_handler(void *socket){

      int sockfd2 = *((int *)&socket);
      int ret_val;
      int n;
      t_tmr *tmp_timer;
      t_tmr *elem2;
      int ret;
      int num_ack_received = 0;
      int len1;
      struct sembuf oper,oper2, oper_filename, oper_timer,oper_sum,oper_window;
      int flag_cycle = 0;
      int round = 0;
      timer_t ret_timer;
      sigset_t  set;
      float percentage;
      int int_p = 0, tmp_p;
      

      sigfillset(&set);
      if(pthread_sigmask(SIG_SETMASK, &set, NULL) != 0){
        perror("\npthread_sigmask error");
        exit(-1);
      }

      // creazione del timer master per l'upload
      oper_timer.sem_num = 0;
      oper_timer.sem_op = -1;
      oper_timer.sem_flg = 0;

wait23:
      if((semop(semid_timer, &oper_timer, 1)) == -1){
          perror("Error wait 23\n");
          if(errno == EINTR){
            goto wait23;
          }
          exit(-1);
      }

      master_timer = 1;

      ret_timer = srtSchedule();
      if(ret_timer == -1){

      }
      

      
      //append non dovrebbe servire
      master_timer = 0;
      master_exists_flag = 1;

      oper_timer.sem_num = 0;
      oper_timer.sem_op = 1;
      oper_timer.sem_flg = 0;

signal25:
      if((semop(semid_timer, &oper_timer, 1)) == -1){
          perror("Error signaling 43\n");
          if(errno == EINTR){
            goto signal25;
          }
          exit(-1);
      }


      while(1){

          len1 = sizeof(servaddr1);
          
          t_data *new_data = malloc(sizeof(t_data));
          if(new_data == NULL){
              perror("\nmalloc error");
              exit(-1);
          }
          
          // wait per aspettare che il main thread mi dia la possibilità di ascoltare ack da parte del server
          oper2.sem_num = 0;
          oper2.sem_op = -1;
          oper2.sem_flg = 0;

wait26:
          if((semop(semid2,&oper2,1)) == -1){
             perror("Error waiting\n");
             if(errno == EINTR){
              goto wait26;
             }
             exit(-1);
          }

rerecvfrom_ack_upload:

          n = recvfrom(sockfd2, new_data, 1500, 0, (struct sockaddr*)&servaddr1, (socklen_t *)&len1);
          if(n < 0){
            perror("\n recvfrom error");
            if(errno == EINTR){
              goto rerecvfrom_ack_upload;
            }
            exit(-1);
          }

          if(n == 0){
            if(close(sockfd2) == -1){
              perror("errore in close");
              exit(1);
            }
          }

          
          oper_timer.sem_num = 0;
          oper_timer.sem_op = -1;
          oper_timer.sem_flg = 0;

rewait_timer:

          if((semop(semid_timer,&oper_timer,1)) == -1){
            perror("Error signaling 2\n");
            if(errno == EINTR){
              goto rewait_timer;
            }
            exit(-1);
          }



          for(tmp_timer = list_head_tmr; tmp_timer != NULL; tmp_timer = tmp_timer->next){
            
              if(atoi(new_data->ack_number) == (atoi(tmp_timer->sequence_number) + (int)strlen(tmp_timer->message) + (int)strlen(tmp_timer->operation_no))){
                 if(last_ack != atoi(new_data->ack_number)){
                    
                    if((last_ack < atoi(new_data->ack_number)) && ((last_ack + (int)strlen(tmp_timer->operation_no) + (int)strlen(tmp_timer->message)) == atoi(new_data->ack_number))){
                        // sto ricevendo ack in ordine
#ifdef ADPTO
                        ret = timer_gettime(tmp_timer->idTimer, &ltr);
                        if(ret == -1){
                          perror("\n timer_gettime error");
                          exit(-1);
                        }

                        po = (1/(pow(10,9)));
                        sample_int = ltr.it_value.tv_sec;
                        sample_dec = (float)(ltr.it_value.tv_nsec *po);

                        sample_rtt = tmp_timer->sample_value +(tmp_timer->last_rto - (float)(sample_int + sample_dec)); // tempo trascorso tra invio pkt e ricezione del suo ack

                        estimated_rtt = (1 - ALPHA)*(estimated_rtt) + ALPHA*sample_rtt;

                        if(first_timeot_turn == 1){
                          dev = sample_rtt/2;
                          first_timeot_turn = 0;
                        }else{
                          dev = (1 - BETA)*dev + BETA*abs(sample_rtt-estimated_rtt);
                        }

                        int dummy_rto = estimated_rtt + 4*dev;

                        if(dummy_rto > MIN_TIMEO && dummy_rto < MAX_TIMEO){
                          rto = dummy_rto;
                        }else if(dummy_rto <= MIN_TIMEO){
                          rto = MIN_TIMEO;
                        }
                        
#endif                        
                        
                        if(upload_filename == 1){

                        // signal per abiliate invio contenuto file da parte del main thread
                          oper_filename.sem_num = 0;
                          oper_filename.sem_op = 1;
                          oper_filename.sem_flg = 0;
resignal_filename:
                          if((semop(semid_filename, &oper_filename, 1)) == -1){
                            perror("Error signaling 2\n");
                            if(errno == EINTR){
                              goto resignal_filename;
                            }
                            exit(-1);
                            
                          }

                        }

                        ret = timer_delete(tmp_timer->idTimer);
                        
                        if(ret == -1){
                          perror("\ntimer_delete error");
                          if(errno == EINTR){
                             // closing connection
                          }
                          exit(-1);
                        }

                        delete_sent_packet(tmp_timer->sequence_number, &list_head_sent_pkts);
                        delete_id_timer(tmp_timer->idTimer, &list_head_id_tmr);
                        delete_timer(tmp_timer->idTimer, &list_head_tmr);
                        

                        // sum_ack = 0;  // fare wait e signal intorno a questa modifica di sum_ack
                        oper_sum.sem_num = 0;
                        oper_sum.sem_op = -1;
                        oper_sum.sem_flg = 0;
              

wait27:                       
                         //wait sul valore del numero files
                        if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                          perror("Error signaling 9\n");
                          if(errno == EINTR){
                            goto wait27;
                          }
                          exit(-1);
                        }

                        sum_ack = 0; // avendo ricevuto in ordine azzero il numero di duplicati ricevuti

                        oper_sum.sem_num = 0;
                        oper_sum.sem_op = 1;
                        oper_sum.sem_flg = 0;

signal28:              
                        //wait sul valore del numero files
                        if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                          perror("Error signal 28\n");
                          if(errno == EINTR){
                            goto signal28;
                          }
                          exit(-1);
                        }

                        last_ack = atoi(new_data->ack_number);
                        num_ack_received++;

                        if(upload_filename == 0){

                          oper_window.sem_num = 0;
                          oper_window.sem_op = 1;
                          oper_window.sem_flg = 0;
              
rewindow2:
                          if((semop(semid_window, &oper_window, 1)) == -1){
                             perror("Error close 2\n");
                             if(errno == EINTR){
                              goto rewindow2;
                             }
                             exit(-1);
                          }

                         

                          percentage = (double)((double)num_ack_received/ ((double)file_content + 2.0)) * 100.0;
                          
                          tmp_p = (int)(percentage);

                          if(tmp_p > int_p){
                              int_p = tmp_p;
                              if(int_p == 100){
                                printf("\n 100 %c", '%');
                                printf("\n Completed!");
                                fflush(stdout);
                              }
                          }
                          
                        }




                        if(upload_filename == 1){
                          upload_filename = 0;
                        }


                        if(num_ack_received >= file_content + 2){
                          free(new_data);
                          
                          ack_up_exp_switch = (3*num_ack_received); 
                          expected_next_seq_num += ack_up_exp_switch;

                          //se caso iniziale metto l'expected uguale a num acks
                          //dove num acks riferito a richiesta + contenuto+nome file+666

                          all_ack_upload_received = 1;
                          file_content = 0;
                          num_ack_received = 0; 

                          retr_phase = 0;
                          upload_filename = 0;  // già dovrebbe essere messo a 0
                          sum_ack = 0;  // già dovrebbe essere messo a 0

                          oper_timer.sem_num = 0;
                          oper_timer.sem_op = 1;
                          oper_timer.sem_flg = 0;

resignal_timer_fin_upload:

                          if((semop(semid_timer,&oper_timer,1)) == -1){
                            perror("Error signaling 2\n");
                            if(errno == EINTR){
                              goto resignal_timer_fin_upload;
                            }
                            exit(-1);
                          }


                          //signal
                          oper.sem_num = 0;
                          oper.sem_op = 1;
                          oper.sem_flg = 0;

resignal_final_upload:

                          if((semop(semid,&oper,1)) == -1){
                            perror("Error signaling 2\n");
                            if(errno == EINTR){
                              goto resignal_final_upload;
                            }
                            exit(-1);
                          }

                          pthread_exit((void *)&ret_val);
                        }
                        else{
                          free(new_data);
                          break;
                        }
                      
                    }
                    else{

                       // ricezione di ack non in ordine

                       if((last_ack < atoi(new_data->ack_number)) && (((last_ack + (int)strlen(tmp_timer->operation_no) + (int)strlen(tmp_timer->message)) != atoi(new_data->ack_number)))){
                          if((last_ack + (int)strlen(tmp_timer->operation_no) + (int)strlen(tmp_timer->message)) < atoi(new_data->ack_number)){
                            
                            last_ack = atoi(new_data->ack_number);

                            oper_window.sem_num = 0;
                            oper_window.sem_op = -1;
                            oper_window.sem_flg = 0;
                
rewindowy:
                            if((semop(semid_sum_ack,&oper_window,1)) == -1){
                                 perror("Error close 2\n");
                                 if(errno == EINTR){
                                  goto rewindowy;
                                 }
                               exit(-1);
                            }

                            sum_ack = 0;


                            oper_window.sem_num = 0;
                            oper_window.sem_op = 1;
                            oper_window.sem_flg = 0;
                

rewindowsa:
                            if((semop(semid_sum_ack,&oper_window,1)) == -1){
                                 perror("Error close 2\n");
                                 if(errno == EINTR){
                                  goto rewindowsa;
                                 }
                               exit(-1);
                            }

#ifdef ADPTO
                            ret = timer_gettime(tmp_timer->idTimer, &ltr);
                            if(ret == -1){
                              perror("\n timer_gettime error");
                              exit(-1);
                            }

                            po = (1/(pow(10,9)));
                            sample_int = ltr.it_value.tv_sec;
                            sample_dec = (float)(ltr.it_value.tv_nsec *po);

                            sample_rtt = tmp_timer->sample_value +(tmp_timer->last_rto - (float)(sample_int + sample_dec)); // tempo trascorso tra invio pkt e ricezione del suo ack

                            estimated_rtt = (1 - ALPHA)*(estimated_rtt) + ALPHA*sample_rtt;

                            if(first_timeot_turn == 1){
                              dev = sample_rtt/2;
                              first_timeot_turn = 0;
                            }else{
                              dev = (1 - BETA)*dev + BETA*abs(sample_rtt-estimated_rtt);
                            }

                            int dummy_rto = estimated_rtt + 4*dev;

                            if(dummy_rto > MIN_TIMEO && dummy_rto < MAX_TIMEO){
                              rto = dummy_rto;
                            }else if(dummy_rto <= MIN_TIMEO){
                              rto = MIN_TIMEO;
                            }
                            
#endif    
                            

                            for(elem2 = list_head_tmr; elem2 != NULL; elem2 = elem2->next){
                              
                                //print_list_tmr(list_head_tmr);
                                round++;
                              
                                if(elem2->next == NULL){
                                  flag_cycle = 1;
                                }

                                if(atoi(elem2->sequence_number) < atoi(new_data->ack_number)){

                                  ret = timer_delete(elem2->idTimer);
                                  
                                  if(ret == -1){
                                    if(errno == EINVAL){
                                      perror("\neinval error");
                                      exit(-1);
                                    }

                                    perror("\ntimer_delete error");
                                    exit(-1);
                                  }

                                  
                                  delete_sent_packet(elem2->sequence_number, &list_head_sent_pkts);
                                  delete_id_timer(elem2->idTimer, &list_head_id_tmr);
                                  delete_timer(elem2->idTimer, &list_head_tmr);
                                  
                                  num_ack_received++;

                                  percentage = (double)((double)num_ack_received/ ((double)file_content + 2.0)) * 100.0;
                                  
                                  tmp_p = (int)(percentage);

                                  if(tmp_p > int_p){
                                      int_p = tmp_p;
                                      if(int_p == 100){
                                        printf("\n 100 %c", '%');
                                        printf("\n Completed!");
                                        fflush(stdout);
                                      }
                                  }
                                          


                                  oper_window.sem_num = 0;
                                  oper_window.sem_op = 1;
                                  oper_window.sem_flg = 0;
                        

rewindow3:
                                  if((semop(semid_window,&oper_window,1)) == -1){
                                      perror("Error close 2\n");
                                      if(errno == EINTR){
                                        goto rewindow3;
                                      }
                                      exit(-1);
                                  }

                                  if(flag_cycle == 1){
                                    flag_cycle = 0;
                                    break;
                                  }
                                  else{
                                    continue;
                                  }

                                }


                                //se ho ricevuto tutti gli acks allora --> pthread_exit((void *)&ret_val);
                                
                                if(num_ack_received >= file_content + 2){
                                  
                                  ack_up_exp_switch = (3*num_ack_received); 
                                  expected_next_seq_num += ack_up_exp_switch;

                                  all_ack_upload_received = 1;
                                  file_content = 0;
                                  num_ack_received = 0; 
                                  retr_phase = 0;
                                  upload_filename = 0;  // già dovrebbe essere messo a 0
                                  sum_ack = 0;  // già dovrebbe essere messo a 0

                                  oper_timer.sem_num = 0;
                                  oper_timer.sem_op = 1;
                                  oper_timer.sem_flg = 0;
resignal_timer_fin_upload2:
                                  //signal dopo aver eventualmente eliminato timer multipli
                                  if((semop(semid_timer, &oper_timer, 1)) == -1){
                                    perror("Error waiting 14\n");
                                    if(errno == EINTR){
                                      goto resignal_timer_fin_upload2;
                                    }
                                    exit(-1);
                                  }

                                  oper.sem_num = 0;
                                  oper.sem_op = 1;
                                  oper.sem_flg = 0;
resignal_final_upload2:
                                  //signal dopo aver eventualmente eliminato timer multipli
                                  if((semop(semid, &oper, 1)) == -1){
                                    perror("Error waiting 14\n");
                                    if(errno == EINTR){
                                      goto resignal_final_upload2;
                                    }
                                    exit(-1);
                                  }

                                  
                                  ret_val = 666;
                                  pthread_exit((void *)&ret_val);
                                }else{
                                  if(flag_cycle == 1){
                                    flag_cycle = 0;
                                    break;
                                  }else{
                                    continue;
                                  }
                                }
                              
                            }

                            if(num_ack_received >= file_content + 2){
                              
                              ack_up_exp_switch = (3*num_ack_received); 
                              expected_next_seq_num += ack_up_exp_switch;

                              all_ack_upload_received = 1;
                              file_content = 0;
                              num_ack_received = 0; 
                              retr_phase = 0;
                              upload_filename = 0;  // già dovrebbe essere messo a 0
                              sum_ack = 0;  // già dovrebbe essere messo a 0
                              
                              oper_timer.sem_num = 0;
                              oper_timer.sem_op = 1;
                              oper_timer.sem_flg = 0;
resignal_timer_fin_upload3:
                              //signal dopo aver eventualmente eliminato timer multipli
                              if((semop(semid_timer, &oper_timer, 1)) == -1){
                                perror("Error waiting 14\n");
                                if(errno == EINTR){
                                  goto resignal_timer_fin_upload3;
                                }
                                exit(-1);
                              }

                              oper.sem_num = 0;
                              oper.sem_op = 1;
                              oper.sem_flg = 0;
resignal_final_upload3:
                              //signal dopo aver eventualmente eliminato timer multipli
                              if((semop(semid, &oper, 1)) == -1){
                                perror("Error waiting 14\n");
                                if(errno == EINTR){
                                  goto resignal_final_upload3;
                                }
                                exit(-1);
                              }

                              ret_val = 666;
                              pthread_exit((void *)&ret_val);

                            }else{
                              break; //grandissimo dubbio
                            }
                          }
                       }
                    }
                 }
                 else{
                    

                    if(last_ack == atoi(new_data->ack_number)){
                      
                      oper_sum.sem_num = 0;
                      oper_sum.sem_op = -1;
                      oper_sum.sem_flg = 0;

signal30:
                      //wait su semaforo per ack cumulativo
                      if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                        perror("Error waiting 17\n");
                        if(errno == EINTR){
                          goto signal30;
                        }
                        exit(-1);
                      }

                      sum_ack++;
                      
                      // handler tre pacchetti duplicati
                      if(sum_ack == 3){
                        // retransmit last ack correctly sent
                        three_duplicate_message(new_data->ack_number);  // da implementare con un solo parametro
                        sum_ack = 0; 
                      }

                      oper_sum.sem_num = 0;
                      oper_sum.sem_op = 1;
                      oper_sum.sem_flg = 0;

signal31:
                      //signal su semaforo per ack cumulativo
                      if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                        perror("Error waiting 17\n");
                        if(errno == EINTR){
                          goto signal31;
                        }
                        exit(-1);
                      }
                    }

                    break;  
                 }
              }
              else{


                //probabilmente ack duplicato
                
                if(last_ack == atoi(new_data->ack_number)){
                  

                  oper_sum.sem_num = 0;
                  oper_sum.sem_op = -1;
                  oper_sum.sem_flg = 0;

wait31:
                  //wait su semaforo per ack cumulativo
                  if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                    perror("Error wait 31\n");
                    if(errno == EINTR){
                          goto wait31;
                        }
                    exit(-1);
                  }

                  sum_ack++;
                  
                  // handler tre pacchetti duplicati
                  if(sum_ack == 3){
                    // retransmit last ack correctly sent
                    three_duplicate_message(new_data->ack_number);   // implements
                    sum_ack = 0; 
                  }

                  oper_sum.sem_num = 0;
                  oper_sum.sem_op = 1;
                  oper_sum.sem_flg = 0;

signal32:
                  //signal su semaforo per ack cumulativo
                  if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                    perror("Error waiting 17\n");
                    if(errno == EINTR){
                          goto signal32;
                        }
                    exit(-1);
                  }

                  break;
                }

                  
              }
          }

          oper_timer.sem_num = 0;
          oper_timer.sem_op = 1;
          oper_timer.sem_flg = 0;

resignal_timer:
      
          
          if((semop(semid_timer,&oper_timer,1)) == -1){
            perror("Error signaling 2\n");
            if(errno == EINTR){
              goto resignal_timer;
            }
            exit(-1);
          }

          

      }
}


// funzione incaricata di gestire la ricezione dei pacchetti proveniente dal server in caso di operazione di 
// list e download .. questi non sono pacchetti semplici pacchetti di ack, ma hanno anche del contenuto nel campo message 
void packet_handler(t_data *packet){

  t_tmr *n;
  timer_t ret_timer;
  int ret;
  int act_seq_num;
  float loss_p;
  float percentage;
  int int_p = 0, tmp_p;

  if(receive_window < 1){
    return;
  }


  if(handler_list_of_files == 1){

      if(second_part_download == 1){
        name_download = 0;
      }

      servaddr1.sin_port = htons(atoi(packet->port));
      
      if(expected_next_seq_num == atoi(packet->sequence_number)){
          //scelta tra uno e due
          if(second_part_download == 1){
            // scrittura sul file in caso di arrivo del contenuto del download
            if(strcmp(packet->message,"666") != 0){
              if(file_size >= (long int)sizeof(packet->message)){
                if((fwrite(packet->message, (size_t)sizeof(packet->message), 1, file_down)) == 0){
                  
                  fflush(stdout);
                  
                }

                file_size -= (long int)sizeof(packet->message);

                percentage = (double)(((double)sizef - (double)file_size)/ (double)sizef) * 100.0;
                
                tmp_p = (int)(percentage);

                if(tmp_p > int_p){
                    int_p = tmp_p;
                    if(int_p == 100){
                      printf("\n 100 %c", '%');
                      printf("\n Completed!");
                      fflush(stdout);
                    }
                }
              }else{
                if((fwrite(packet->message, (size_t)file_size, 1, file_down)) == 0){
                  fflush(stdout);
                  
                }

                file_size -= file_size;

                percentage = (double)(((double)sizef - (double)file_size)/ (double)sizef) * 100.0;
                
                
                tmp_p = (int)(percentage);

                if(tmp_p > int_p){
                    int_p = tmp_p;
                    if(int_p == 100){
                      printf("\n 100 %c", '%');
                      printf("\n Completed!");
                      fflush(stdout);
                    }
                }
              }
              
            }
            
          }else{
            //lista o lista nell' operazione di download
            printf("-> %s\n", packet->message); 
            counter++;
          }
          

         
          t_data *new_data = malloc(sizeof(t_data));
          if(new_data == NULL){
              perror("\nmalloc error");
              exit(-1);
          }

          // popolo new_data->sequence
          asprintf(&temp_buf,"%d",num_pkt);
          strcpy(new_data->sequence_number,temp_buf);

          // popolo new->ack_number
          act_seq_num = atoi(packet->sequence_number) + (int)strlen(packet->message) + (int)strlen(packet->operation_no);

refor:         
          // da verificare se la debufferizzazione avviene in ordine
          for(t_buffered_pkt *n = list_head_buffered_pkts; n != NULL; n = n->next){
            if(act_seq_num == atoi(n->sequence_number)){
              
                // utilizzo del pacchetto
                act_seq_num = atoi(n->sequence_number) + (int)strlen(n->message) + (int)strlen(n->operation_no);
                
                if(second_part_download == 1){
                  //utilizzo
                  if(strcmp(n->message,"666") != 0){
                    if(file_size >= (long int)sizeof(n->message)){
                      if((fwrite(n->message, sizeof(n->message), 1, file_down)) == 0){
                        fflush(stdout);
                        
                      }


                      file_size -= (long int)sizeof(n->message);

                      percentage = (double)(((double)sizef - (double)file_size)/ (double)sizef) * 100.0;
                      
                      tmp_p = (int)(percentage);

                      if(tmp_p > int_p){
                          int_p = tmp_p;
                          if(int_p == 100){
                            printf("\n 100 %c", '%');
                            printf("\n Completed!");
                            fflush(stdout);
                          }
                      }
                    }else{
                      if((fwrite(n->message, (size_t)file_size, 1, file_down)) == 0){
                        fflush(stdout);
                        
                      }

                      file_size -= file_size;

                      percentage = (((double)sizef - (double)file_size)/ (double)sizef) * 100.0;
                      
                      
                      tmp_p = (int)(percentage);

                      if(tmp_p > int_p){
                          int_p = tmp_p;
                          if(int_p == 100){
                            printf("\n 100 %c", '%');
                            printf("\n Completed!");
                            fflush(stdout);
                          }
                      }
                
                      fflush(stdout);
                    }
                    

                    
                  }else{
                    //gestione chiusura con ack
                    download_closing = 1;
                  }
                  
                }else{
                  printf("-> %s\n", n->message); 
                  counter++;
                }
                
                num_pkt_buff--;

                // potrebbe non avere senso -- visto che facciamo if sull'uguale 
                if(act_seq_num < atoi(n->sequence_number) + (int)strlen(n->message) + (int)strlen(n->operation_no)){
                    act_seq_num = atoi(n->sequence_number) + (int)strlen(n->message) + (int)strlen(n->operation_no);
                }

                delete_buffered_packet(n->sequence_number, &list_head_buffered_pkts);

                
                receive_window++;
                goto refor;  // faccio ricominciare il ciclo nel caso in cui i pacchetti siano in ordine dopo l'aggiornamento appena fatto 
            }
          }

          

          asprintf(&acknum, "%d", act_seq_num);
          strcpy(new_data->ack_number, acknum);
      
          // popolo il new_dat->operation_no
          strcpy(new_data->operation_no,packet->operation_no);

          // popolo il new_data->message
          memcpy(new_data->message, def_str, sizeof(def_str));
          num_pkt = num_pkt + DEF_LENGTH;

          timer_t ret_timer;

          //scelta tra uno e due
          if(counter == num_of_files + 1 && second_part_download == 0){
            closing_ack = 1;
            flag_last_ack_sent = 1;
          }

          loss_p = float_rand(0.0, 1.0);
          
          if(loss_p >= LOSS_PROBABILITY){
              if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                perror("errore in sendto 4 (4)");
                exit(1);
              }
          }

          //aggiorno ultimo ack correttamente mandato
          //update_last_sent_ack(new_data->sequence_number, new_data->ack_number, new_data->operation_no, new_data->message);

          //scelta tra uno e due
          if(second_part_download ==1){
              //eventualmente gestire chiusura con 666
              if((strcmp(packet->message, "666") == 0 ) || download_closing == 1){
                
                flag_last_ack_sent = 1;
                ret_timer = srtSchedule();
                
#ifdef ADPTO
                append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
                append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);                
#endif
                
                closing_ack = 1; //nel caso di download e lista avrò un solo timer quindi posso riconoscerlo con questo flag
                                  //ma non varrà per l'upload
              }
              
          }else{

            if(counter == num_of_files+1){
              
              flag_last_ack_sent = 1;
              ret_timer = srtSchedule();
              
#ifdef ADPTO
              append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
              append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);
#endif
              
              closing_ack = 1; //nel caso di download e lista avrò un solo timer quindi posso riconoscerlo con questo flag
                                //ma non varrà per l'upload
            }

          }
          

          last_pack++;

          memset(acknum, 0, (int)strlen(acknum));
          
          expected_next_seq_num = act_seq_num;
          free(new_data);
          
      }
      else{

        if(atoi(packet->sequence_number) < expected_next_seq_num){
          //pacchetto che sicuramente ho già ricevuto
          

          //necessario capire che sto ricevendo il nome del file da scaricare
          //mi arriva un pkt vecchio, probabilmente devo reinviare SEMPRE un ack-->lo faccio sotto
          if(atoi(packet->message) != 0){
            
            t_data *new_data5 = malloc(sizeof(t_data));
            if(new_data5 == NULL){
              perror("\nmalloc error");
              exit(-1);
            }

            // popolo new_data->sequence
            asprintf(&temp_buf,"%d",(num_pkt - 2));
            strcpy(new_data5->sequence_number,temp_buf);

            int act = atoi(packet->sequence_number) + (int)strlen(packet->message) + (int)strlen(packet->operation_no);

            // popolo new->ack_number
            asprintf(&temp_buf,"%d",act);
            strcpy(new_data5->ack_number, temp_buf);
       
            // popolo il new_dat->operation_no
            if(managing_list == 1){
              strcpy(new_data5->operation_no,"1\0");
            }
            else{
              strcpy(new_data5->operation_no,"2\0");
            }
            
            // popolo il new_data->message
            memcpy(new_data5->message, def_str, sizeof(def_str));
            //num_pkt = num_pkt + strlen(new_data->message) + strlen(new_data->operation_no);
            
            loss_p = float_rand(0.0, 1.0);
            
            if(loss_p >= LOSS_PROBABILITY){
              if(sendto(sockfd, new_data5, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                perror("errore in sendto 4 (5)");
                exit(1);
              }
            }
           
            free(new_data5);
            

          }else{
            //pkt relativo ad effettivo contenuto--->mando un ack duplicato relativo all'ultimo ack che ho mandato
            //secondo me qui viene mandato a prescindere un ack e quindi anche per il nome file
            

            t_data *new_data_ar = malloc(sizeof(t_data));
            if(new_data_ar == NULL){
              perror("\nmalloc error");
              exit(-1);
            }


            // popolo new_data->sequence
            asprintf(&temp_buf,"%d",(num_pkt - 2));
            strcpy(new_data_ar->sequence_number,temp_buf);

            act_seq_num = expected_next_seq_num;


            // popolo new->ack_number
            asprintf(&acknum,"%d",act_seq_num);
            strcpy(new_data_ar->ack_number, acknum);
            
            // popolo il new_data->message
            memcpy(new_data_ar->message, def_str, sizeof(def_str));
            num_pkt = num_pkt + DEF_LENGTH;
            
            loss_p = float_rand(0.0, 1.0);
            
            if(loss_p >= LOSS_PROBABILITY){
              if(sendto(sockfd, new_data_ar, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                perror("errore in sendto 4 (6)");
                exit(1);
              }
            }

            memset(acknum, 0, (int)strlen(acknum));
            
            free(new_data_ar);

          }

        }else{        
          //expected_next_seq_num > atoi(packet->sequence_number)
          // bufferizzare.. 
          if(++num_pkt_buff <= (receive_window-1)){
              //packets_buffer = reallocarray(packets_buffer, num_pkt_buff, 1500);
              append_buffered_packet(packet->sequence_number, packet->ack_number, packet->message, packet->operation_no, &list_head_buffered_pkts);
              receive_window--;
          }else{
              num_pkt_buff--;
          }

          //nel caso mi arrivi un pkt fuori sequenza ---> io mando a prescindere ack duplicato
          t_data *new_data = malloc(sizeof(t_data));
          if(new_data == NULL){
              perror("\nmalloc error");
              exit(-1);
          }

          // popolo new_data->sequence
          asprintf(&temp_buf,"%d",num_pkt);
          strcpy(new_data->sequence_number,temp_buf);

          // popolo new->ack_number
          act_seq_num = expected_next_seq_num;

          asprintf(&acknum, "%d", act_seq_num);
          strcpy(new_data->ack_number, acknum);
      
          // popolo il new_data->message
          memcpy(new_data->message, def_str, sizeof(def_str));
          num_pkt = num_pkt + DEF_LENGTH;

          loss_p = float_rand(0.0, 1.0);
          
          if(loss_p >= LOSS_PROBABILITY){
            if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
              perror("errore in sendto 4 (7)");
              exit(1);
            }
          }


          last_pack++;  

          
          memset(acknum, 0, (int)strlen(acknum));
          
          free(new_data);


        }
      }
    
  }

  if(((atoi(packet->operation_no) == 1) || (atoi(packet->operation_no) == 2)) && ((expected_next_seq_num < 0) || (prev_is_upload == 1))){
    if(expected_next_seq_num < 0){
       expected_next_seq_num = -(int)strlen(packet->message);
    }
    else{
      expected_next_seq_num -= ((int)strlen(packet->message)-1);
      prev_is_upload = 0;
    }
    
  }

  if((atoi(packet->operation_no) == 3) && (prev_is_upload == 0)){
     
     if(numoffiles != NULL){
        expected_next_seq_num += ((int)strlen(numoffiles) -1);
     }
     
  }
 
  for(n = list_head_tmr; n != NULL; n = n->next){

    
    if((atoi(packet->ack_number) == ((atoi(n->sequence_number) + (int)strlen(n->message) + (int)strlen(n->operation_no))))){
      
      if(last_ack != atoi(packet->ack_number)){
        
        num_files = atoi(packet->message);
        
        if((last_ack < atoi(packet->ack_number)) && ((expected_next_seq_num + (int)strlen(packet->message) + (int)strlen(packet->operation_no) -1) == (atoi(packet->sequence_number)))){
            // nel caso di lista e download ci entriamo solamente per riscontrare la ricezione dell'ack della richiesta
            // Caso in cui va tutto bene, sto ricevendo ack sulla richiesta in ordine di sequenza
            // ricevo primo ack su un pkt
            // ricevo il primo pacchetto utilr -- > non avrò mai nulla di bufferizzato
#ifdef ADPTO
            ret = timer_gettime(n->idTimer, &ltr);
            if(ret == -1){
              perror("\n timer_gettime error");
              exit(-1);
            }

            po = (1/(pow(10,9)));
            sample_int = (float)ltr.it_value.tv_sec;
            sample_dec = (float)(ltr.it_value.tv_nsec *po);

            sample_rtt = n->sample_value + (n->last_rto - (float)(sample_int + sample_dec)); // tempo trascorso tra invio pkt e ricezione del suo ack

            estimated_rtt = (1 - ALPHA)*(estimated_rtt) + ALPHA*sample_rtt;

            if(first_timeot_turn == 1){
              dev = sample_rtt/2;
              first_timeot_turn = 0;
            }else{
              dev = (1 - BETA)*dev + BETA*abs(sample_rtt-estimated_rtt);
            }

            int dummy = estimated_rtt + 4*dev;

            if(dummy > MIN_TIMEO && dummy < MAX_TIMEO){
              rto = dummy;
            }else if(dummy <= MIN_TIMEO){
              rto = MIN_TIMEO;
            }
           
#endif 
            
            
            ret = timer_delete(n->idTimer);
            
            if(ret == -1){
              perror("\ntimer_delete error");
              exit(-1);
            }

            if(errno == EINTR){
              //closing connection
            }

            //print_list_tmr(&list_head_tmr);
            delete_id_timer(n->idTimer, &list_head_id_tmr);
            delete_timer(n->idTimer, &list_head_tmr);
            
            //receive_window++; // perché quando ricevo un ack in ordine devo aumentare la recv_window ? 
            last_ack = atoi(packet->ack_number);
        

            switch(atoi(packet->operation_no)){

              case 1:

                servaddr1.sin_port = htons(atoi(packet->port));
                managing_list = 1;
                
                // gestione degli ack di risposta del client verso il server 
                // per confermare la ricezione dei sigoli file della list 
              
              
                t_data *new_data = malloc(sizeof(t_data));
                if(new_data == NULL){
                  perror("\nmalloc error");
                  exit(-1);
                }

                // popolo new_data->sequence
                asprintf(&temp_buf,"%d",num_pkt);
                strcpy(new_data->sequence_number,temp_buf);

                // popolo new->ack_number
                act_seq_num = atoi(packet->sequence_number) + (int)strlen(packet->message) + (int)strlen(packet->operation_no); // ultimo ack mandato
                asprintf(&acknum, "%d", act_seq_num);
                strcpy(new_data->ack_number, acknum);
           
                // popolo il new_dat->operation_no
                strcpy(new_data->operation_no,"1\0");

                // popolo il new_data->message
                memcpy(new_data->message, def_str, sizeof(def_str));
                num_pkt = num_pkt + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);
                
                loss_p = float_rand(0.0, 1.0);
                
                if(loss_p >= LOSS_PROBABILITY){
                   if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                      perror("errore in sendto 4 (8)");
                      exit(1);
                    }
                }
               

                memset(acknum, 0, (int)strlen(acknum));
                expected_next_seq_num = act_seq_num; // ultimo ack mandato

                free(new_data);
                
                handler_list_of_files = 1;
                  
                
              
                break;

              case 2:

                servaddr1.sin_port = htons(atoi(packet->port));
                porta_send_download_file = atoi(packet->port_download);
                
                t_data *new_data_download = malloc(sizeof(t_data));
                if(new_data_download == NULL){
                  perror("\nmalloc error");
                  exit(-1);
                }

                // popolo new_data->sequence
                asprintf(&temp_buf,"%d",num_pkt);
                strcpy(new_data_download->sequence_number,temp_buf);

                // popolo new->ack_number
                act_seq_num = atoi(packet->sequence_number) + (int)strlen(packet->message) + (int)strlen(packet->operation_no); // ultimo ack mandato
                asprintf(&acknum, "%d", act_seq_num);
                strcpy(new_data_download->ack_number, acknum);
           
                // popolo il new_dat->operation_no
                strcpy(new_data_download->operation_no,"2\0");

                // popolo il new_data->message
                if(strcmp(packet->message,"404") == 0){
                  memcpy(new_data_download->message, "404\0", MAXLINE);
                  //qui devo gestire come se fosse una chiusura
                }else{
                  memcpy(new_data_download->message, def_str, sizeof(def_str));
                }
                
                num_pkt = num_pkt + (int)strlen(new_data_download->message) + (int)strlen(new_data_download->operation_no);
                
                loss_p = float_rand(0.0, 1.0);
                
                if(loss_p >= LOSS_PROBABILITY){
                    if(sendto(sockfd, new_data_download, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                      perror("errore in sendto 4 (9)");
                      exit(1);
                    }
                }

                if(strcmp(packet->message, "404") == 0){
                  flag_last_ack_sent = 1;
                  ret_timer = srtSchedule();
                  
#ifdef ADPTO
                  append_timer_adaptive(ret_timer, new_data_download->sequence_number, &list_head_tmr,new_data_download->SYNbit,new_data_download->ACKbit,new_data_download->ack_number,new_data_download->message,new_data_download->operation_no,new_data_download->FINbit,new_data_download->port, new_data_download->size, rto, rto, sample_value, 0);
#else
                  append_timer(ret_timer, new_data_download->sequence_number, &list_head_tmr,new_data_download->SYNbit,new_data_download->ACKbit,new_data_download->ack_number,new_data_download->message,new_data_download->operation_no,new_data_download->FINbit,new_data_download->port, new_data_download->size);                  
#endif
                  
                  closing_ack = 1; //nel caso di download e lista avrò un solo timer quindi posso riconoscerlo con questo flag
                                    //ma non varrà per l'upload
                }

                memset(acknum, 0, (int)strlen(acknum));
              
                expected_next_seq_num = act_seq_num; // ultimo ack mandato
                
                //window--;
                free(new_data_download);

                handler_list_of_files = 1;
                
                
                break;

              case 3:
                // nella fase di richiesta dell'upload mi basta fare la delete del timer se ricevo il messaggio di 'OK' del server
                
                break;
            }

            break;




        }else{
            
            if((strcmp(packet->operation_no, "2") == 0) && strcmp(packet->message, "404") == 0){

              servaddr1.sin_port = htons(atoi(packet->port));
              porta_send_download_file = atoi(packet->port_download);
              
            
              t_data *new_data_download = malloc(sizeof(t_data));
              if(new_data_download == NULL){
                perror("\nmalloc error");
                exit(-1);
              }

              // popolo new_data->sequence
              asprintf(&temp_buf,"%d",num_pkt);
              strcpy(new_data_download->sequence_number,temp_buf);

              // popolo new->ack_number
              act_seq_num = atoi(packet->sequence_number) + (int)strlen(packet->message) + (int)strlen(packet->operation_no); // ultimo ack mandato
              asprintf(&acknum, "%d", act_seq_num);
              strcpy(new_data_download->ack_number, acknum);
         
              // popolo il new_dat->operation_no
              strcpy(new_data_download->operation_no,"2\0");

              // popolo il new_data->message
              memcpy(new_data_download->message, "404\0", MAXLINE);
                
              num_pkt = num_pkt + (int)strlen(new_data_download->message) + (int)strlen(new_data_download->operation_no);
              
              loss_p = float_rand(0.0, 1.0);
              
              if(loss_p >= LOSS_PROBABILITY){
                if(sendto(sockfd, new_data_download, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                  perror("errore in sendto 4 (1)");
                  exit(1);
                }
              }

              ret = timer_delete(list_head_tmr->idTimer);
              if(ret == -1){
                perror("\ntimer_delete error");
                exit(-1);
              }
              
              delete_id_timer(list_head_id_tmr->tidp, &list_head_id_tmr);
              delete_timer(list_head_tmr->idTimer, &list_head_tmr);

              
              flag_last_ack_sent = 1;
              ret_timer = srtSchedule();
              
#ifdef ADPTO
              append_timer_adaptive(ret_timer, new_data_download->sequence_number, &list_head_tmr,new_data_download->SYNbit,new_data_download->ACKbit,new_data_download->ack_number,new_data_download->message,new_data_download->operation_no,new_data_download->FINbit,new_data_download->port, new_data_download->size, rto, rto, sample_value, 0);
#else
              append_timer(ret_timer, new_data_download->sequence_number, &list_head_tmr,new_data_download->SYNbit,new_data_download->ACKbit,new_data_download->ack_number,new_data_download->message,new_data_download->operation_no,new_data_download->FINbit,new_data_download->port, new_data_download->size);
#endif
              
              
              closing_ack = 1; //nel caso di download e lista avrò un solo timer quindi posso riconoscerlo con questo flag
                              //ma non varrà per l'upload
              
              memset(acknum, 0, (int)strlen(acknum));
              
              expected_next_seq_num = act_seq_num; // ultimo ack mandato
              
             
              free(new_data_download);

              write_on_file = 0;  // metto write_on_file a 0 per evitare che dopo la packet_handler venga creato il file
              return;

              
            }

            // casi da gestire:
            // 1. last_ack >= packet->ack_number && (expected_next_seq_num + strlen(packet->message) + strlen(packet->operation_no) - 1) == atoi(packet->sequence_number)
            // 1. ---> sarà così solo nel caso della lista, download (forse) ---> implementare switch case
            // 2. last_ack >= packet->ack_number && (expected_next_seq_num + strlen(packet->message) + strlen(packet->operation_no) - 1) != atoi(packet->sequence_number)
            // 2. ---> buffering, però la prima condizione implica che ci possono anche essere vecchi
            // 3. last_ack < packet->ack_number && (expected_next_seq_num + strlen(packet->message) + strlen(packet->operation_no) - 1) != atoi(packet->sequence_number)
            // 3. ---> buffering

            // ricevo in ordine ma è un pacchetto per il quale ho già ricevuto l'ack
            if((last_ack >= atoi(packet->ack_number)) && ((expected_next_seq_num + (int)strlen(packet->message) + (int)strlen(packet->operation_no) - 1) == atoi(packet->sequence_number))){
                switch(atoi(packet->operation_no)){
                    case 1:
                      // don't care... la prima condizione nell'if si verifica sempre alla ricezione dell'ack
                      // non mi è arrivato l'ack per la richiesta di lista...
                      printf("\n Non mi è arrivato l'ack per la richiesta di lista (1)");
                      fflush(stdout);
                      break;
                    case 2:
                      // don't care... la prima condizione nell'if si verifica sempre alla ricezione dell'ack
                      // non mi è arrivato l'ack per la richiesta di lista...
                      printf("\n Non mi è arrivato l'ack per la richiesta di download (1)");
                      fflush(stdout);
                      break;
                    case 3:
                      break;
                }
            }

            if((last_ack >= atoi(packet->ack_number)) && ((expected_next_seq_num + (int)strlen(packet->message) + (int)strlen(packet->operation_no) - 1) != atoi(packet->sequence_number))){
                switch(atoi(packet->operation_no)){
                    case 1:
                      // don't care
                      // non dovrebbe essere mai acceduto questo case se il server riuscisse a calcolarsi l'ack in modo corretto
                      printf("\n Non mi è arrivato l'ack per la richiesta di lista (2)");
                      fflush(stdout);
                      break;
                    case 2:
                      // don't care
                      // non dovrebbe essere mai acceduto questo case se il server riuscisse a calcolarsi l'ack in modo corretto
                      printf("\n Non mi è arrivato l'ack per la richiesta di download (2)");
                      fflush(stdout);
                      break;
                    case 3:
                      break;
                }
                
            }

            if((last_ack < packet->ack_number) && ((expected_next_seq_num + (int)strlen(packet->message) + (int)strlen(packet->operation_no) - 1) != atoi(packet->sequence_number))){
                switch(atoi(packet->operation_no)){
                    case 1:
                      // può darsi che si perda il pacchetto contenente l'ack contenente ed il numero di file, e mi arriri direttamente il pacchetto 
                      // con l'ack atteso ma messaggio errato (es. pippo.txt) 
                      if((expected_next_seq_num + (int)strlen(packet->message) + (int)strlen(packet->operation_no) - 1) < atoi(packet->sequence_number)){
                         // buffering
                          ret = timer_delete(n->idTimer);
                          printf("\n**** TIMER DELETED ****");
                          fflush(stdout);
                      
                          if(ret == -1){
                            perror("\ntimer_delete error");
                            exit(-1);
                          }

                          if(errno == EINTR){
                            //closing connection
                          }

                          delete_id_timer(n->idTimer, &list_head_id_tmr);
                          delete_timer(n->idTimer, &list_head_tmr);
                          /*
resignal_win:                      
                          oper_window.sem_num = 0;
                          oper_window.sem_op = 1;
                          oper_window.sem_flg = 0;

                          //wait su fine chiusura finale
                          if((semop(semid_window, &oper_window, 1)) == -1){
                             perror("Error resignal_win\n");
                             if(errno ==  EINTR){
                              goto resignal_win;
                             }
                             exit(-1);
                          }*/

                          last_ack = atoi(packet->ack_number);
                          if(++num_pkt_buff <= (receive_window-1)){
                            append_buffered_packet(packet->sequence_number, packet->ack_number, packet->message, packet->operation_no, &list_head_buffered_pkts);
                            receive_window--;
                            
                          }else{
                            num_pkt_buff--;
                            
                          }
                          
                      }
                      else{
                         // mi dovrebbe mandare un contenuto relativo ad una richiesta precedente --> maybe impossible 
                      }

                      break;
                    case 2:

                      // può darsi che si perda il pacchetto contenente l'ack contenente ed il numero di file, e mi arriri direttamente il pacchetto 
                      // con l'ack atteso ma messaggio errato (es. pippo.txt) 
                      if((expected_next_seq_num + (int)strlen(packet->message) + (int)strlen(packet->operation_no) - 1) < atoi(packet->sequence_number)){
                         // buffering
                          ret = timer_delete(n->idTimer);
                         
                          if(ret == -1){
                            perror("\ntimer_delete error");
                            exit(-1);
                          }

                          if(errno == EINTR){
                            //closing connection
                          }

                          delete_id_timer(n->idTimer, &list_head_id_tmr);
                          delete_timer(n->idTimer, &list_head_tmr);
                          

                          last_ack = atoi(packet->ack_number);
                          if(++num_pkt_buff <= (receive_window-1)){
                            append_buffered_packet(packet->sequence_number, packet->ack_number, packet->message, packet->operation_no, &list_head_buffered_pkts);
                            receive_window--;
                            
                          }else{
                            num_pkt_buff--;
                            
                          }
                          
                      }
                      else{
                         // mi dovrebbe mandare un contenuto relativo ad una richiesta precedente --> maybe impossible 
                      }
                      break;
                    case 3:
                      break;
                }

                break;

            }

        }
      }else{ 
         
          // ho ricevuto un ack uguale all'ultimo ack ricevuto --> gestiamo gli ack duplicati
          switch(atoi(packet->operation_no)){
            case 1:
              // la ricezione di ack duplicati non è un problema nella lista perché si può perdere solamente il primo ed unico ack
              // don't care
              // non dovrebbe essere mai acceduto questo case se il server riuscisse a calcolarsi l'ack in modo corretto
              printf("\n Non mi è arrivato l'ack per la richiesta di lista (3)");
              fflush(stdout);
              
              break;

            case 2:
              // don't care
              // non dovrebbe essere mai acceduto questo case se il server riuscisse a calcolarsi l'ack in modo corretto
              printf("\n Non mi è arrivato l'ack per la richiesta di download (3)");
              fflush(stdout);

              break;

            case 3:
              break;
          
          }
      
      }
    
    }
  }
}


int main(int argc, char **argv) 
{ 
  
  
  struct    sigaction act, act2;
  sigset_t  set;
  int       maxd, inserted_choice;
  fd_set    rset;
  char      sendline[MAXLINE];
  int       n, ret; 
  char      choice[3];
  char      cho;
  const     char hello_msg[1] = "0"; 
  //int       count_down = 0;
  int       cont=0; //serve per gestire il nome del file ed il contenuto
  FILE      *file_upload;
  int       fd_down;
  struct    dirent **namelist;
  int       act_ins_up = 0;
  int       act_ins_down = 0;
  int       num2;
  int       port_fin2;
  char      path[MAXLINE];
  char      slave[10];
  int       slave2;
  int       slave3;
  char      slave_laph[10];
  pthread_t tid_upload, tid_rt;
  int       port;
  timer_t   ret_timer;
  struct    sembuf oper1;
  struct    sembuf oper2;
  struct    sembuf oper3;
  struct    sockaddr_in myNewAddr,sin, servaddr3;
  int       sockfd_ack_upload;
  int       prev_is_download = 0;
  float     loss_p;
  long int  size;
  char      *size_file;
  struct    sembuf oper_timer; 
  struct    sembuf oper_filename;
  struct    sembuf oper_sum,oper_window;
  int       act_seq_num;
  char      *value;
  int       file_not_found = 1;
  int       enabler = 0;
  sigset_t  block;
  
  sigemptyset(&block);
  sigaddset(&block, 34);
  sigaddset(&block, SIGUSR1);
  pthread_sigmask(SIG_BLOCK, &block, NULL);

  pthread_create(&tid_rt, NULL, signal_handler_thread, NULL);

  sleep(1);
  sigemptyset(&block);
  sigaddset(&block, SIGUSR1);
  pthread_sigmask(SIG_UNBLOCK, &block, NULL);

  if(argc != 2){
    fprintf(stderr, "utilizzo: udpClient <indirizzo IP server>\n");
    exit(1);
  }

  sigfillset(&set);
  act.sa_mask = set;
  act.sa_sigaction = handler;
  act.sa_flags = 0;
  sigaction(SIGINT, &act, NULL);

  /* Set up signal handler. */
  
  act2.sa_flags = SA_SIGINFO;
  act2.sa_sigaction = sig_usr_handler;
  sigemptyset(&act2.sa_mask);
  if (sigaction(SIGUSR1, &act2, NULL) == -1){
      perror("\n sigaction error");
      return(-1);
  }
  
  // Creating socket file descriptor 
  if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) { 
    printf("socket creation failed"); 
    exit(0); 
  } 

  memset((void *)&servaddr, 0, sizeof(servaddr)); 
  // Filling server information 
  servaddr.sin_family = AF_INET; 
  servaddr.sin_port = htons(PORT); 
  if(inet_aton(argv[1], &servaddr.sin_addr) <= 0){
    fprintf(stderr, "errore in inet_aton\n");
    exit(1);
  }

  bind(sockfd, (struct sockaddr*)&servaddr, sizeof(servaddr));

  semid = semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666);
  if(semid == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid,0,SETVAL,0) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid,-1,IPC_RMID,NULL);
    exit(-1);
  }

  semid2 = semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666);
  if(semid2 == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid2,0,SETVAL,0) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid2,-1,IPC_RMID,NULL);
    exit(-1);
  }

  semid3 = semget(IPC_PRIVATE,1,IPC_CREAT|IPC_EXCL|0666);
  if(semid3 == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid3,0,SETVAL,0) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid3,-1,IPC_RMID,NULL);
    exit(-1);
  }

  semid_filename = semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666);
  if(semid_filename == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid_filename, 0, SETVAL, 0) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid_filename, -1, IPC_RMID, NULL);
    exit(-1);
  }

  semid_timer = semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666);
  if(semid_timer == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid_timer, 0, SETVAL, 1) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid_filename, -1, IPC_RMID, NULL);
    exit(-1);
  }

  
  semid_sum_ack = semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666);
  if(semid_sum_ack == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid_sum_ack, 0, SETVAL, 1) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid_sum_ack, -1, IPC_RMID, NULL);
    exit(-1);
  }

  semid_retr_turn = semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666);
  if(semid_retr_turn == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid_retr_turn, 0, SETVAL, 1) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid_retr_turn, -1, IPC_RMID, NULL);
    exit(-1);
  }

  semid_handshake = semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666);
  if(semid_handshake == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid_handshake, 0, SETVAL, 0) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid_handshake, -1, IPC_RMID, NULL);
    exit(-1);
  }

  semid_window = semget(IPC_PRIVATE, 1, IPC_CREAT|IPC_EXCL|0666);
  if(semid_window == -1){
    perror("Error creating semaphore one\n");
    exit(-1);
  }

  if(semctl(semid_window, 0, SETVAL, WIND_SIZE_T) == -1){
    perror("Unable to set the sem to zero value\n");
    semctl(semid_window, -1, IPC_RMID, NULL);
    exit(-1);
  }



  t_data *new_data = malloc(sizeof(t_data));
  if(new_data == NULL){
    perror("malloc error");
    exit(1);
  }

  //mi alloco memoria per riferimento ad ultimo ack mandato
  last_sent_ack = malloc(sizeof(t_data));
  if(last_sent_ack == NULL){
    perror("malloc error");
    exit(1);
  }

  //memset(sendline, 0, MAXLINE);
  
  srand(time(NULL));
  num = rand()%10000;
  snprintf(new_data->sequence_number, sizeof(new_data->sequence_number), "%d", num);
  snprintf(new_data->ack_number, sizeof(new_data->ack_number),"%d",a_num);
  strcpy(new_data->SYNbit, "1\0");
  strcpy(new_data->ACKbit, "0\0");
  strcpy(new_data->operation_no,"0\0");
  memcpy(sendline, hello_msg, sizeof(hello_msg));
  
  loss_p = float_rand(0.0, 1.0);
  
  if(loss_p >= LOSS_PROBABILITY){
      if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
          perror("errore in sendto 3");
          exit(1);
      }
  }   

  inserted_choice = 0; //per fase di handshake
  printf("SENT first handshake msg SYN\n");

  ret_timer = srtSchedule();
  
#ifdef ADPTO
  append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
  append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);
#endif
  
  memset(sendline, 0, MAXLINE);  
  free(new_data);


  if(end_hs == 1){

rechoice:
  
  choice_menu();
  if((ret = scanf("%s", choice)) != 0){
             
        if((int)strlen(choice) > 1){
                
            if(choice[1] != '\n'){
                printf("\nWARNING: invalid choice.\n");
                flush(stdin);
                memset(choice, 0, sizeof(char)*((int)strlen(choice)+1));
                goto rechoice;
                    
            }
                
        }
    }else{
      printf("\nchoice not valid.. retry\n");
      goto rechoice;
    }
    
  
    inserted_choice = atoi(choice);
    //printf("\n");
    fflush(stdout);

    switch(inserted_choice){
                
        case 1:
          printf("\n -------> List selected\n");
          memset(sendline, 0, MAXLINE);

          t_data *new_data = malloc(sizeof(t_data));
          if(new_data == NULL){
            perror("malloc error");
            exit(1);
          }

          // valorizzo il campo sequence number
          asprintf(&temp_buf,"%d",num_pkt);
          strcpy(new_data->sequence_number,temp_buf);

          // valorizzo il campo operantion_no
          memcpy(sendline, choice, sizeof(choice));
          strcpy(new_data->operation_no,sendline);

          // valorizzo il campo message
          memcpy(new_data->message, def_str, sizeof(def_str));
       
          num_pkt = num_pkt + DEF_LENGTH;

          
          loss_p = float_rand(0.0, 1.0);
          
          if(loss_p >= LOSS_PROBABILITY){
            if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
              perror("errore in sendto 4 (2)");
              exit(1);
            }
          }
          
          
          ret_timer = srtSchedule();
#ifdef ADPTO
          append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
          append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);
#endif
          
          prev_is_download = 0;
          first_timeot_turn = 1;
          
          expected_ack = expected_ack + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);
          
          free(new_data);

          break;
               
        case 2:
            printf("\n -------> Download selected\n");
            memset(sendline, 0, MAXLINE);
            
            t_data *new_data1 = malloc(sizeof(t_data));
            if(new_data1 == NULL){
              perror("malloc error");
              exit(1);
            }

            // valorizzo il campo sequence number
            asprintf(&temp_buf,"%d",num_pkt);
            strcpy(new_data1->sequence_number,temp_buf);

            // valorizzo il campo operation_no
            memcpy(sendline, choice, sizeof(choice));
            strcpy(new_data1->operation_no,sendline);

            // valorizzo il campo message
            memcpy(new_data1->message, def_str, sizeof(def_str));
        
            num_pkt = num_pkt + DEF_LENGTH;

            loss_p = float_rand(0.0, 1.0);
            
            if(loss_p >= LOSS_PROBABILITY){
                if(sendto(sockfd, new_data1, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                    perror("errore in sendto 4 (3)");
                    exit(1);
                }
            }
            
            
            ret_timer = srtSchedule();
#ifdef ADPTO
            append_timer_adaptive(ret_timer, new_data1->sequence_number, &list_head_tmr, new_data1->SYNbit, new_data1->ACKbit, new_data1->ack_number, new_data1->message, new_data1->operation_no, new_data1->FINbit,new_data1->port, new_data1->size, rto, rto, sample_value, 0);
#else
            append_timer(ret_timer, new_data1->sequence_number, &list_head_tmr, new_data1->SYNbit, new_data1->ACKbit, new_data1->ack_number, new_data1->message, new_data1->operation_no, new_data1->FINbit,new_data1->port, new_data1->size);            
#endif
            
            prev_is_download = 1;
            first_timeot_turn = 1;
            expected_ack = expected_ack + (int)strlen(new_data1->message) + (int)strlen(new_data1->operation_no);
            
            free(new_data1);
          
            break;
                
        case 3:
            printf("\n -------> Upload selected\n");


            t_data *new_data2 = malloc(sizeof(t_data));
            if(new_data2 == NULL){
              perror("malloc error");
              exit(1);
            }

            memset(sendline, 0, MAXLINE);
            memcpy(sendline, choice, sizeof(choice));                
            
            asprintf(&temp_buf,"%d",num_pkt);
            strcpy(new_data2->sequence_number,temp_buf);
            
            strcpy(new_data2->operation_no,sendline);

            memcpy(new_data2->message, def_str, sizeof(def_str));
            
            num_pkt = num_pkt + DEF_LENGTH;

            loss_p = float_rand(0.0, 1.0);
            
            if(loss_p >= LOSS_PROBABILITY){
              if(sendto(sockfd, new_data2, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                perror("errore in sendto 5");
                exit(1);
              }
            }
           
            ret_timer = srtSchedule();
#ifdef ADPTO
            append_timer_adaptive(ret_timer, new_data2->sequence_number, &list_head_tmr,new_data2->SYNbit,new_data2->ACKbit,new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit,new_data2->port, new_data2->size, rto, rto, sample_value, 0);
#else
            append_timer(ret_timer, new_data2->sequence_number, &list_head_tmr,new_data2->SYNbit,new_data2->ACKbit,new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit,new_data2->port, new_data2->size);            
#endif            
            
            expected_ack = expected_ack + (int)strlen(new_data2->message) + (int)strlen(new_data2->operation_no);
            first_timeot_turn = 1;
            
            free(new_data2);

              
            break;
               
        case 4:
            printf("\nStarting Closing Phase\n");
            fflush(stdout);
            quit();

            break;
                  
        default:
            
            if(switch_fin == 1){
               inserted_choice = 4;
               break;
            }

            printf("WARNING: invalid choice.\nReinsert the option\n");
            fflush(stdout);
            goto rechoice;
    
        }
  
  } 

    

  while(1){
    FD_ZERO(&rset);
    //flush(stdin);
    FD_SET(fileno(stdin), &rset);
    FD_SET(sockfd, &rset);
    maxd = (fileno(stdin) < sockfd) ? (sockfd + 1) : (fileno(stdin) + 1);
    fflush(stdout);

reselect:
    
    if(select(maxd, &rset, NULL, NULL, NULL) < 0){
       if(errno == EINTR){
          goto reselect;
       }else{
          perror("errore in select");
          exit(1);
       }
       
    }

    
    /* Controlla se il socket è leggibile */
      if (FD_ISSET(sockfd, &rset)) { 
          
          int len = sizeof(servaddr);
          int len2 = sizeof(servaddr1);

rerecv:
          //printf("rerecv");
          fflush(stdout);
          t_data *new_data = malloc(sizeof(t_data));
          if(new_data == NULL){
            perror("malloc error");
            exit(1);
          }

          if(end_hs == 0){
              //ricevo dal padre in fase di hs o chiusura connessione
              
              if((n = recvfrom(sockfd, new_data, 1500, 0, (struct sockaddr*)&servaddr, (socklen_t *)&len)) < 0){
                
                if(errno == EINTR && flag_final_timer == 1){
                  if(flag_last_cl_msg_nsend == 0){
                      // sono qui se l'ultimo pkt in chiusura da parte del client è stato inviato
                      oper3.sem_num = 0;
                      oper3.sem_op = -1;
                      oper3.sem_flg = 0;

rewaiths3:
                      //wait su fine chiusura finale
                      if((semop(semid_handshake, &oper3, 1)) == -1){
                         perror("Error rewait_hs\n");
                         if(errno ==  EINTR){
                          goto rewaiths3;
                         }
                         exit(-1);
                      }
                      printf("\nClient OFF");
                      fflush(stdout);
                      close(sockfd);
                      exit(0);
                  }
                  else{
                    // sono qui se l'ultimo pkt in fase di chiusura del client NON è stato inviato --> lo rimando
                    flag_last_cl_msg_nsend = 0;
                    flag_final_timer = 0;
                    printf("\n(Mando l'ultimo ACK dal client\n");
                    t_data *new_data_cl = malloc(sizeof(t_data));
                    if(new_data_cl == NULL){
                        perror("malloc error");
                        exit(1);
                    }

                    strcpy(new_data_cl->FINbit,"1\0");
                    strcpy(new_data_cl->ACKbit,"1\0");

                    snprintf(new_data_cl->ack_number,sizeof(new_data_cl->ack_number),"%d",slave3+1);
                    strcpy(new_data_cl->operation_no, "\0");
        
                    loss_p = float_rand(0.0,1.0);
                    
                    if(loss_p >= LOSS_PROBABILITY){
                      if(sendto(sockfd, new_data_cl, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                         perror("errore in sendto 6");
                         exit(1);
                      }
                    }
                    
                    oper_timer.sem_num = 0;
                    oper_timer.sem_op = -1;
                    oper_timer.sem_flg = 0;

signal33:
                    if((semop(semid_timer, &oper_timer, 1)) == -1){
                       perror("Error signaling 48\n");
                       if(errno == EINTR){
                          goto signal33;
                        }
                       exit(-1);
                    }

                    t_tmr *timer;

                    for(timer = list_head_tmr; timer!=NULL; timer=timer->next){
                        if(strcmp(timer->ACKbit,"1") == 0){
                          ret = timer_delete((list_head_tmr)->idTimer);
                          delete_id_timer((list_head_id_tmr)->tidp,&list_head_id_tmr);
                          delete_timer((list_head_tmr)->idTimer,&list_head_tmr);
                        }
                    }

                    if(loss_p < LOSS_PROBABILITY){
                      flag_last_cl_msg_nsend = 1;
                    }

                    flag_last_ack_sent = 1;
                    flag_final_timer = 1;
                    ret_timer = srtSchedule();
#ifdef ADPTO
                    append_timer_adaptive(ret_timer, new_data_cl->sequence_number, &list_head_tmr, new_data_cl->SYNbit, new_data_cl->ACKbit, new_data_cl->ack_number, new_data_cl->message, new_data_cl->operation_no, new_data_cl->FINbit,new_data_cl->port, new_data_cl->size, rto, rto, sample_value, 0);
#else
                    append_timer(ret_timer, new_data_cl->sequence_number, &list_head_tmr, new_data_cl->SYNbit, new_data_cl->ACKbit, new_data_cl->ack_number, new_data_cl->message, new_data_cl->operation_no, new_data_cl->FINbit,new_data_cl->port, new_data_cl->size);                
#endif              
                    
                    oper_timer.sem_num = 0;
                    oper_timer.sem_op = 1;
                    oper_timer.sem_flg = 0;

signal34:
                    if((semop(semid_timer, &oper_timer, 1)) == -1){
                       perror("Error signaling 49\n");
                       if(errno == EINTR){
                          goto signal34;
                        }
                       exit(-1);
                    }
                   
                   
                    
                    free(new_data_cl);
                    free(new_data);

                    goto rerecv;
                  }
                  
                }

                if(errno == EINTR && flag_close_wait == 1){

                  oper3.sem_num = 0;
                  oper3.sem_op = -1;
                  oper3.sem_flg = 0;

rewait_sig_laph:
                  //wait su fine hs
                  if((semop(semid_handshake, &oper3, 1)) == -1){
                     perror("Error rewait_sig_laph\n");
                     if(errno ==  EINTR){
                      goto rewait_sig_laph;
                     }
                     exit(-1);
                  }



                 // invio dopo interrupt dell'ultimo pacchetto in caso di ctrl+c dal server
                  printf("\nMando il primo FINbit dal client");
                  fflush(stdout);
                  t_data *new_data_cl1 = malloc(sizeof(t_data));
                  if(new_data_cl1 == NULL){
                      perror("malloc error");
                      exit(1);
                  }
                  
                  //srand(time(NULL));
                  num2 = rand()%10000;
                  strcpy(new_data_cl1->FINbit,"1\0");
                  snprintf(new_data_cl1->sequence_number,sizeof(new_data_cl1->sequence_number),"%d",num2);
                  strcpy(new_data_cl1->ACKbit,"0\0");
                  strcpy(new_data_cl1->ack_number,"0\0");

                 
                  oper_timer.sem_num = 0;
                  oper_timer.sem_op = -1;
                  oper_timer.sem_flg = 0;

rewait_laph:
                  //wait su fine chiusura finale
                  if((semop(semid_timer, &oper_timer, 1)) == -1){
                     perror("Error rewait_laph\n");
                     if(errno ==  EINTR){
                      goto rewait_laph;
                     }
                     exit(-1);
                  }

                  loss_p = float_rand(0.0, 1.0);
                  
                  if(loss_p >= LOSS_PROBABILITY){
                     if(sendto(sockfd, new_data_cl1, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                       perror("errore in sendto 8");
                       exit(1);
                    }
                  }

                  flag_last_ack_sent = 0;

                  delete_id_timer(list_head_id_tmr->tidp, &list_head_id_tmr);
                  delete_timer(list_head_tmr->idTimer, &list_head_tmr);

                  ret_timer = srtSchedule();
                  
#ifdef ADPTO
                  append_timer_adaptive(ret_timer, new_data_cl1->sequence_number, &list_head_tmr,new_data_cl1->SYNbit,new_data_cl1->ACKbit,new_data_cl1->ack_number,new_data_cl1->message,new_data_cl1->operation_no,new_data_cl1->FINbit,new_data_cl1->port, new_data_cl1->size, rto, rto, sample_value, 0);
#else
                  append_timer(ret_timer, new_data_cl1->sequence_number, &list_head_tmr,new_data_cl1->SYNbit,new_data_cl1->ACKbit,new_data_cl1->ack_number,new_data_cl1->message,new_data_cl1->operation_no,new_data_cl1->FINbit,new_data_cl1->port, new_data_cl1->size);                  
#endif
                  
                  oper_timer.sem_num = 0;
                  oper_timer.sem_op = 1;
                  oper_timer.sem_flg = 0;

resignal_laph:
                  //wait su fine chiusura finale
                  if((semop(semid_timer, &oper_timer, 1)) == -1){
                     perror("Error resignal_laph\n");
                     if(errno ==  EINTR){
                      goto resignal_laph;
                     }
                     exit(-1);
                  }

                  free(new_data_cl1);
                  free(new_data);
                  printf("\nLAST_ACK");               
                  fflush(stdout);

                  goto rerecv;

              }


              if(errno == EINTR && flag_last_ack_sent == 1){

                oper3.sem_num = 0;
                oper3.sem_op = -1;
                oper3.sem_flg = 0;

rewait_hs:
                //wait su fine hs
                if((semop(semid_handshake, &oper3, 1)) == -1){
                   perror("Error rewait_hs\n");
                   if(errno ==  EINTR){
                    goto rewait_hs;
                   }
                   exit(-1);
                }

                servaddr.sin_port = htons(PORT);

                printf("\nESTAB Client\n");
                fflush(stdout);
                system("clear");
                end_hs = 1;  // per dire che l'handsahke è terminato
                flag_last_ack_sent = 0;
                

                oper3.sem_num = 0;
                oper3.sem_op = 1;
                oper3.sem_flg = 0;

resignal_win3:
                //wait su fine hs
                if((semop(semid_handshake, &oper3, 1)) == -1){
                   perror("Error resignal_win3\n");
                   if(errno ==  EINTR){
                    goto resignal_win3;
                   }
                   exit(-1);
                }
                goto rechoice;
              }else if(errno == EINTR && flag_last_ack_sent == 0){
                goto rerecv;
              }
              exit(1);
            }

            servaddr.sin_port = htons(PORT);
            //packet_handler(new_data);

          }else{
              
              //ricevo dai thread in caso di rihiesta 1 2 3
              if((n = recvfrom(sockfd, new_data, 1500, 0, (struct sockaddr*)&servaddr1, (socklen_t *)&len2)) < 0){
                
                if((errno == EINTR) && (closing_ack == 1)){
                    if(managing_list == 1){
                        // chiusura della lista -->  mostro 'Do you want to...' 
                        
                        oper3.sem_num = 0;
                        oper3.sem_op = -1;
                        oper3.sem_flg = 0;

wait35:
                        //wait
                        if((semop(semid3, &oper3, 1)) == -1){
                          perror("Error waiting 1\n");
                          if(errno == EINTR){
                            goto wait35;
                          }
                          exit(-1);
                        }

                        closing_ack = 0;
                        flag_last_ack_sent = 0;
                        managing_list = 0;
                        prev_is_upload = 0;
                        counter = 0;
                        errno = 0;

                        retr_phase = 0;

                        
                        printf("\n\nDo you want to go back to menu? \ndigit (y/n)..\n");
                        fflush(stdout);
rescan_closing:                        
                        
                        flush(stdin);
                        ret = scanf("%c", &cho);
                        
                        if(ret == EOF){
                          if(errno == EINTR){
                            continue;
                          }
                        }

                        if(ret != 0){
                          if(cho == 'y' || cho == 'Y'){
                            system("clear");
                            choice_menu();
                            continue;
                          }else{
                            if(cho == 'n' || cho == 'N'){
                              ////////////////////////////////////////////////////////////////////////////////
                              // prima di fare exit devo comunicare al server che sto per chiudere, il server dovrà eliminarmi dalla 
                              // sua lista
                              // realizzare una chiusura migliore di exit(1)
                              exit(1);
                            }else{
                              flush(stdin);
                              printf("input not valid.. please reinsert y/n!\n");
                              goto rescan_closing;
                            }
                          }
                        }else{
                          printf("no input.. please reinsert y/n!\n");
                          goto rescan_closing;
                        }

                        flush(stdin);
                    }
                    else{
                      //parte download->o chiudo lista con la variabile a zero o la chiusura del download definitiva
                      if(second_part_download == 1){
                        //è la fine effettiva del download

                        // chiusura della lista -->  mostro 'Do you want to...' 
                        
                        oper3.sem_num = 0;
                        oper3.sem_op = -1;
                        oper3.sem_flg = 0;

wait37:
                        //wait
                        if((semop(semid3, &oper3, 1)) == -1){
                          perror("Error waiting 1\n");
                          if(errno == EINTR){
                            goto wait37;
                          }
                          exit(-1);
                        }

                        closing_ack = 0;
                        flag_last_ack_sent = 0;
                        managing_list = 0;
                        download_closing = 0;
                        second_part_download = 0;
                        write_on_file = 0;
                        cont = 0;
                        counter = 0;
                        switch_mean_d = 0;
                        act_ins_down = 0;
                        prev_is_upload = 0;
                        handler_list_of_files = 0;
                        asprintf(&numoffiles, "%d", num_of_files);
                        expected_next_seq_num -= (int)strlen(numoffiles);
                        errno = 0;

                        retr_phase = 0;

                        if(file_down != NULL){
                          if(fclose(file_down) != 0){
                            perror("\nfclose error");
                            exit(-1);
                          }

                          file_down = NULL;
                        }
                        
                        printf("\n\nDo you want to go back to menu? \ndigit (y/n)..\n");
                        fflush(stdout);
rescan_closing2:                        
                        
                        flush(stdin);
                        ret = scanf("%c", &cho);
                        
                        if(ret == EOF){
                          if(errno == EINTR){
                            continue;
                          }
                        }

                        if(ret != 0){
                          if(cho == 'y' || cho == 'Y'){
                            system("clear");
                            choice_menu();
                            continue;
                          }else{
                            if(cho == 'n' || cho == 'N'){
                              ////////////////////////////////////////////////////////////////////////////////
                              // prima di fare exit devo comunicare al server che sto per chiudere, il server dovrà eliminarmi dalla 
                              // sua lista
                              // realizzare una chiusura migliore di exit(1)
                              exit(1);
                            }else{
                              flush(stdin);
                              printf("input not valid.. please reinsert y/n!\n");
                              goto rescan_closing2;
                            }
                          }
                        }else{
                          printf("no input.. please reinsert y/n!\n");
                          goto rescan_closing2;
                        }

                        flush(stdin);
                      }else{
                        // chiusura della lista nel caso del download --> mostro 'Please insert the name of the file...'

                        // chiusura della lista -->  mostro 'Do you want to...' 
                        
                        oper3.sem_num = 0;
                        oper3.sem_op = -1;
                        oper3.sem_flg = 0;

wait38:
                        //wait
                        if((semop(semid3, &oper3, 1)) == -1){
                          perror("Error waiting 1\n");
                          if(errno == EINTR){
                            goto wait38;
                          }
                          exit(-1);
                        }

                        closing_ack = 0;
                        flag_last_ack_sent = 0;
                        handler_list_of_files = 0;
                        write_on_file = 1;
                        name_download = 1;

                        
                        printf("\n\nPlease insert the name of the file to download...\n");
                        fflush(stdout);
                        

                        flush(stdin);

                        continue;

                      }
                        

                    }

                    


                }
                else if(errno == EINTR && closing_ack == 0){ // errno == EINTR in caso in cui scatterà il timer 
                  errno = 0;
                  goto rerecv;
                }
               
                
              }

              if(closing_ack == 1){
                if((atoi(new_data->operation_no) == 1) || (atoi(new_data->operation_no) == 2)){

                  servaddr1.sin_port = htons(atoi(new_data->port));

                  int ret3;

                  ret3 = timer_delete(list_head_tmr->idTimer);
                  if(ret3 == -1){
                      perror("\ntimer_delete error");
                      exit(-1);
                  }
                  
                  // elimino timer relativo al vecchio ack cumulativo --> ne mando uno nuovo per la chiusura
                  
                  t_data* new_data2 = malloc(sizeof(t_data));
                  if(new_data2 == NULL){
                    perror("error creating new data\n");
                    exit(-1);
                  }
                  //sono lista o download==>avrò dentro sicuramente solo ack cumulativo finale
                  
                  strcpy(new_data2->sequence_number,list_head_tmr->sequence_number);
                  
                  strcpy(new_data2->ack_number, list_head_tmr->ack_number);

                  strcpy(new_data2->operation_no, list_head_tmr->operation_no);
                  
                  memcpy(new_data2->message, def_str, sizeof(def_str));
                  
                  loss_p = float_rand(0.0, 1.0);
                  
                  if(loss_p > LOSS_PROBABILITY){
                    if(sendto(sockfd, new_data2, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                      perror("errore in sendto 4 (3)");
                      exit(1);
                    }
                  }
                  
                  delete_id_timer(list_head_id_tmr->tidp, &list_head_id_tmr);
                  delete_timer(list_head_tmr->idTimer, &list_head_tmr);

                  
                  ret_timer = srtSchedule();
                  
#ifdef ADPTO
                  append_timer_adaptive(ret_timer, new_data2->sequence_number, &list_head_tmr,new_data2->SYNbit,new_data2->ACKbit,new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit,new_data2->port, new_data2->size, rto, rto, sample_value, 0);
#else
                  append_timer(ret_timer, new_data2->sequence_number, &list_head_tmr,new_data2->SYNbit,new_data2->ACKbit,new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit,new_data2->port, new_data2->size);                  
#endif
                  
                  free(new_data2);
                  free(new_data);

                  goto rerecv;
                }
              }

              packet_handler(new_data);

              
          }

          snprintf(slave,10,"%d",num+1);
          if(((strcmp(new_data->ACKbit,"1"))==0) && ((strcmp(new_data->ack_number,slave))==0) && (client_closing == 1)){

              printf("\n IL SERVER MI HA MANDATO IL PRIMO ACK");
              fflush(stdout);

              oper_timer.sem_num = 0;
              oper_timer.sem_op = -1;
              oper_timer.sem_flg = 0;

wait39:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 44\n");
                 if(errno == EINTR){
                  goto wait39;
                 }
                 exit(-1);
              }

              if(list_head_tmr != NULL && list_head_id_tmr != NULL){
                ret = timer_delete((list_head_tmr)->idTimer);
                delete_id_timer((list_head_id_tmr)->tidp, &list_head_id_tmr);
                delete_timer((list_head_tmr)->idTimer, &list_head_tmr);
              }            
              

              oper_timer.sem_num = 0;
              oper_timer.sem_op = 1;
              oper_timer.sem_flg = 0;

signal40:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 45\n");
                 if(errno == EINTR){
                  goto signal40;
                 }
                 exit(-1);
              }


              goto rerecv;
          }

          snprintf(slave_laph, 10, "%d", num2 + 1);
          if((strcmp(new_data->ACKbit,"1")==0) && (strcmp(new_data->ack_number,slave_laph)==0)){

              oper_timer.sem_num = 0;
              oper_timer.sem_op = -1;
              oper_timer.sem_flg = 0;

signal41:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 46\n");
                 if(errno == EINTR){
                  goto signal41;
                 }
                 exit(-1);
              }


              ret = timer_delete((list_head_tmr)->idTimer);
              delete_id_timer((list_head_id_tmr)->tidp,&list_head_id_tmr);
              delete_timer((list_head_tmr)->idTimer,&list_head_tmr);
              
              printf("\nCLIENT HAS RECEIVED ALL...bye bye\n");
              fflush(stdout);

              oper_timer.sem_num = 0;
              oper_timer.sem_op = 1;
              oper_timer.sem_flg = 0;

signal42:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 47\n");
                 if(errno == EINTR){
                  goto signal42;
                 }
                 exit(-1);
              }

              close(sockfd);
              exit(0);
          }


          
          if(((strcmp(new_data->FINbit,"1"))==0) && (client_closing == 1)){

              // Client ha iniziato la chiusura e dovrà mandare l'ultimo messaggio di chiusura
              
              port_fin2 = ntohs(servaddr.sin_port);
              servaddr3.sin_port = htons(port_fin2);
              inet_aton(argv[1], &servaddr3.sin_addr);
              servaddr3.sin_family = AF_INET;
              
              printf("\nMando l'ultimo ACK dal client\n");
              t_data *new_data_cl = malloc(sizeof(t_data));
              if(new_data_cl == NULL){
                  perror("malloc error");
                  exit(1);
              }

              strcpy(new_data_cl->FINbit,"1\0");
              strcpy(new_data_cl->ACKbit,"1\0");

              slave2 = atoi(new_data->sequence_number);
              slave3 = slave2;
              
              snprintf(new_data_cl->ack_number,sizeof(new_data_cl->ack_number),"%d",slave2+1);
              strcpy(new_data_cl->operation_no, "\0");
  
              loss_p = float_rand(0.0,1.0);
              
              if(loss_p >= LOSS_PROBABILITY){
                if(sendto(sockfd, new_data_cl, 1500, 0, (struct sockaddr *)&servaddr3, (socklen_t)sizeof(servaddr3)) < 0){
                   perror("errore in sendto 6");
                   exit(1);
                }
              }
              
              oper_timer.sem_num = 0;
              oper_timer.sem_op = -1;
              oper_timer.sem_flg = 0;

signal43:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 48\n");
                 if(errno == EINTR){
                  goto signal43;
                 }
                 exit(-1);
              }

              t_tmr *timer;

              for(timer = list_head_tmr; timer!=NULL; timer=timer->next){
                  
                  if(strcmp(timer->ACKbit,"1") == 0){
                    ret = timer_delete((list_head_tmr)->idTimer);
                    delete_id_timer((list_head_id_tmr)->tidp,&list_head_id_tmr);
                    delete_timer((list_head_tmr)->idTimer,&list_head_tmr);
                  }
              }

              if(loss_p < LOSS_PROBABILITY){
                flag_last_cl_msg_nsend = 1;
              }

              flag_last_ack_sent = 1;
              flag_final_timer = 1;
              ret_timer = srtSchedule();
#ifdef ADPTO
              append_timer_adaptive(ret_timer, new_data_cl->sequence_number, &list_head_tmr, new_data_cl->SYNbit, new_data_cl->ACKbit, new_data_cl->ack_number, new_data_cl->message, new_data_cl->operation_no, new_data_cl->FINbit,new_data_cl->port, new_data_cl->size, rto, rto, sample_value, 0);
#else
              append_timer(ret_timer, new_data_cl->sequence_number, &list_head_tmr, new_data_cl->SYNbit, new_data_cl->ACKbit, new_data_cl->ack_number, new_data_cl->message, new_data_cl->operation_no, new_data_cl->FINbit,new_data_cl->port, new_data_cl->size);                
#endif              
              
              oper_timer.sem_num = 0;
              oper_timer.sem_op = 1;
              oper_timer.sem_flg = 0;

signal44:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 49\n");
                 if(errno == EINTR){
                  goto signal44;
                 }
                 exit(-1);
              }
             
             
              //sleep(4);
              free(new_data_cl);
              free(new_data);

              goto rerecv;
              

          }else if(((strcmp(new_data->FINbit,"1") == 0) && (client_closing == 0))){


              // server inizio chiusura con ctrl + c
              end_hs = 0; //lo setto a zero così mi metto in ascolto dal main thread del server
              printf("\nMando il primo ACK dal client");
              t_data *new_data_cl = malloc(sizeof(t_data));
              if(new_data_cl == NULL){
                  perror("malloc error");
                  exit(1);
              }

              strcpy(new_data_cl->ACKbit,"1\0");
              slave2=atoi(new_data->sequence_number);
              snprintf(new_data_cl->ack_number,sizeof(new_data_cl->ack_number),"%d",slave2+1);
              strcpy(new_data_cl->operation_no, "\0"); // per pulire operation_no


              
              oper_timer.sem_num = 0;
              oper_timer.sem_op = -1;
              oper_timer.sem_flg = 0;

rewait_cl_wait:
              //wait
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error waiting timer CLOSE WAIT\n");
                 if(errno == EINTR){
                    errno = 0;
                    goto rewait_cl_wait;
                 }
                 exit(-1);
              }

              loss_p = float_rand(0.0,1.0);
              
              if(loss_p >= LOSS_PROBABILITY){
                 if(sendto(sockfd, new_data_cl, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                   perror("errore in sendto 7");
                   exit(1);
                }
              }

              if(list_head_tmr != NULL){
                 ret = timer_delete(list_head_tmr->idTimer);
                 delete_id_timer(list_head_id_tmr->tidp, &list_head_id_tmr);
                 delete_timer(list_head_tmr->idTimer, &list_head_tmr);
              }
              
              flag_last_ack_sent = 1;
              flag_close_wait = 1;
              ret_timer = srtSchedule();
#ifdef ADPTO
              append_timer_adaptive(ret_timer, new_data_cl->sequence_number, &list_head_tmr, new_data_cl->SYNbit, new_data_cl->ACKbit, new_data_cl->ack_number, new_data_cl->message, new_data_cl->operation_no, new_data_cl->FINbit,new_data_cl->port, new_data_cl->size, rto, rto, sample_value, 0);
#else
              append_timer(ret_timer, new_data_cl->sequence_number, &list_head_tmr, new_data_cl->SYNbit, new_data_cl->ACKbit, new_data_cl->ack_number, new_data_cl->message, new_data_cl->operation_no, new_data_cl->FINbit,new_data_cl->port, new_data_cl->size);              
#endif


              
              oper3.sem_num = 0;
              oper3.sem_op = 1;
              oper3.sem_flg = 0;

resignal_cl_wait:
              //wait
              if((semop(semid_timer, &oper3, 1)) == -1){
                 perror("Error signaling timer CLOSE WAIT\n");
                 if(errno == EINTR){
                    errno = 0;
                    goto resignal_cl_wait;
                 }
                 exit(-1);
              }

              free(new_data_cl);
              printf("\nCLOSE_WAIT");

              goto rerecv;
             

          }

          

          switch(inserted_choice){

               case 0: //handshake

                  snprintf(slave,10,"%d",num+1);

                  if(((strcmp(new_data->ack_number,slave)==0) && (strcmp(new_data->ACKbit,"1")==0))){

                     if(list_head_tmr != NULL){
                       ret = timer_delete(list_head_tmr->idTimer);
                       if(ret == -1){
                        perror("Error deleting in handshake\n");
                        exit(1);
                       }

                     }
                     
                     if(list_head_id_tmr != NULL){
                       delete_id_timer(list_head_id_tmr->tidp,&list_head_id_tmr);
                     }
                     
                     if(list_head_tmr != NULL){
                       delete_timer(list_head_tmr->idTimer,&list_head_tmr);
                     }
                    
                     t_data *new_data_hs = malloc(sizeof(t_data));
                     if(new_data_hs == NULL){
                        perror("malloc error");
                        exit(1);
                     }


                     strcpy(new_data_hs->ACKbit,"1\0");
                     slave2=atoi(new_data->sequence_number);
                     
                     snprintf(new_data_hs->ack_number,sizeof(new_data_hs->ack_number),"%d",slave2+1);
                     loss_p = float_rand(0.0, 1.0);
                     
                     if(loss_p >= LOSS_PROBABILITY){
                         if(sendto(sockfd, new_data_hs, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                          perror("errore in sendto 9");
                          exit(1);
                         }
                      }

                     flag_last_ack_sent = 1;
                     flag_last_hs = 1;
                     

                     ret_timer = srtSchedule();
#ifdef ADPTO                     
                     append_timer_adaptive(ret_timer, new_data_hs->sequence_number, &list_head_tmr, new_data_hs->SYNbit, new_data_hs->ACKbit, new_data_hs->ack_number, new_data_hs->message, new_data_hs->operation_no, new_data_hs->FINbit,new_data_hs->port, new_data_hs->size, rto, rto, sample_value, 0);
#else
                     append_timer(ret_timer, new_data_hs->sequence_number, &list_head_tmr, new_data_hs->SYNbit, new_data_hs->ACKbit, new_data_hs->ack_number, new_data_hs->message, new_data_hs->operation_no, new_data_hs->FINbit,new_data_hs->port, new_data_hs->size);                     
#endif
                     //flag_last_ack_sent = 0;

                     free(new_data_hs);
                     goto rerecv;
                    
                  }

                  break;
     
               case 1:
                  
                  if (n < 4) {
                    fprintf(stderr, "(1)str_clisel_echo: il server ha interrotto l'interazione\n");
                    exit(1);
                  }

                  // counter --> number of files to show 
                  if(counter == 0){
                    //numero di files da ricevere
                    num_of_files = atoi(new_data->message);
                    memset(new_data->message, 0, MAXLINE);
                    //free(new_data);
                    counter++;
                    continue;
                  }

                  
                  if(errno == EINTR){
                    if(ctrlc_signal_flag == 1){
                       goto reselect;
                    }
                    
                  }


                  free(new_data);

                  if(counter == num_of_files+1){
                    counter = 0;
                    handler_list_of_files = 0;
                  
                    asprintf(&numoffiles, "%d", num_of_files);
                    expected_next_seq_num -= (int)strlen(numoffiles);
                    
                    goto rerecv;
                  }
                  else{
                    goto rerecv;
                  }




                  break;

                case 2:

                  if (n < 4) {
                    fprintf(stderr, "str_clisel_echo: il server ha interrotto l'interazione\n");
                    exit(1);
                  }


                  if(counter == 0){
                    //numero di files da ricevere
                    num_of_files = atoi(new_data->message);
                    memset(new_data->message, 0, MAXLINE);
                    counter++;
                    continue;
                  }

                  
                  if(errno == EINTR){
                      if(ctrlc_signal_flag == 1){
                          goto reselect;
                      }
                  }

                  

                  if(write_on_file == 1){
                    
                    if((cont == 0) && (counter == num_of_files + 1)){
                       
                        file_size = strtol(new_data->size, NULL, 10);
                        sizef = strtol(new_data->size, NULL, 10);

                        printf("\n\nOra creo il file con il suo nome:%s (%ld BYTES)\n",new_data->message,sizef);

                        cont++;

                        strcpy(path, "FILES_CLIENT/");
                        strcat(path, new_data->message);

                        fd_down = open(path, O_CREAT| O_TRUNC| O_RDWR, 0666);
                        if(fd_down == -1){
                            perror("open error");
                            exit(1);
                        }

                        file_down = fdopen(fd_down, "w+");
                        if(file_down == NULL){
                           perror("fdopen error");
                           exit(1);
                        }

                    }


                  }

                  if(counter == num_of_files+1){
                    act_ins_down = 1;
                    goto rerecv;
                  }

                  break;

                case 3:

                  //devo gestire se non è la prima richiesta
                  //messaggio di ok
                  
                  if(errno == EINTR){
                    if(ctrlc_signal_flag == 1){
                      fflush(stdout);
                      goto reselect;
                    }
                  }

                  // ho ricevuto il messaggio di ok da parte del server con il quale ho il permesso di iniziare ad inviare il file e il contenuto del file
                  // da caricare sul server
                  if(strcmp(new_data->message, "K") == 0){
                     //inizio upload
                    act_seq_num = atoi(new_data->sequence_number) + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no); 
                    packet_received +=1;

                    system("cd FILES_CLIENT/");
        
                    n = scandir("FILES_CLIENT/", &namelist, file_select, alphasort);
                    if (n == -1) {
                        perror("scandir");
                        exit(EXIT_FAILURE);
                    }

                    printf("\n\nFiles:\n");
                    while (n--) {
                        printf("\n%s", namelist[n]->d_name);   

                    }


                    expected_next_seq_num = act_seq_num;

                    switch_mean_u = 1;
                    act_ins_up = 1;
                    send_content_file_phase = 1;
                    
                    if(packet_received == 1){  // per prevenire che il last_ack sia impari al valore dell'ack che mi manda il server se sto gestendo l'ack del filename faccio +2
                      last_ack += 2; // potenziale errore in caso di upload come non prima operazione
                    }


                    printf("\n\n\nPlease insert the name of the file to upload...\n");
                    fflush(stdout);


                  }else{

                    //rifai richiesta....
                  }


                  break;
                
                default:
                  break;


          }

          
      }


      /* Controlla se il file è leggibile */      
      if (FD_ISSET(fileno(stdin), &rset)) {
        
        //memset(sendline, 0, MAXLINE);

        
rescan2:
        if(file_not_found == 1 && enabler == 1){
          printf("\nPath not..\n\nPlease insert the name of the file to upload: ");
        }
        
        memset(sendline, 0, MAXLINE);
        if((ret = scanf("%s", sendline)) == 0){
            printf("invalid input\n");
            goto rescan2;
        }

        inserted_choice = atoi(sendline);
       
        if(inserted_choice !=1 && inserted_choice!=2 && inserted_choice!=3 && inserted_choice!=4){
          //sto inserendo o il nome del file da downloadare o il file da uploadare
          if(act_ins_up == 1){
              //sono pronto per inserire nome del file da uploadare
              if(client_closing == 1){
                  printf("signal CTRL+C sent\n");
                  fflush(stdout);
                  goto rerecv;
              }              

              n = scandir("FILES_CLIENT/", &namelist, file_select, alphasort);
              if (n == -1) {
                  perror("scandir");
                  exit(EXIT_FAILURE);
              }


              while (n--) {
                  if(strcmp(namelist[n]->d_name, sendline) == 0){
                      act_ins_up = 0;
                      file_not_found = 0;  // se trovo il file file_not_found set to zero
                      break; 
                  }
              }

              
              if(file_not_found == 1){
                 enabler = 1;
                 goto rescan2;
              }

            
              sockfd_ack_upload = socket(AF_INET, SOCK_DGRAM, 0);
              if(sockfd_ack_upload < 0){
                  perror("\nsocket error");
                  exit(-1);
              }

            
              memset((void *)&myNewAddr, 0, sizeof(myNewAddr));
              myNewAddr.sin_family = AF_INET; 
              myNewAddr.sin_addr.s_addr = htonl(INADDR_ANY); 
              myNewAddr.sin_port = htons(0); 
              if(bind(sockfd_ack_upload, (struct sockaddr*)&myNewAddr, sizeof(myNewAddr)) < 0){
                perror("\n bind error on binding for thread ack list\n");
                exit(-1);
              }

              socklen_t len3 = sizeof(sin);

              if (getsockname(sockfd_ack_upload, (struct sockaddr *)&sin, &len3) != 0)
                  perror("Error on getsockname");
                  
              port = ntohs(sin.sin_port);

              ret = pthread_create(&tid_upload, NULL, ack_upload_handler, *((void **)&sockfd_ack_upload));
              if(ret != 0){
                perror("pthread_create error");
                exit(1);
              }

              t_data *new_data_filename = malloc(sizeof(t_data));
              if(new_data_filename == NULL){
                  perror("malloc error");
                  exit(1);
              }

              inserted_choice = 3;

              
              strcpy(path, "FILES_CLIENT/");
              strcat(path, sendline);

              if((file_upload = fopen(path, "r")) == NULL){
                  perror("fopen error");
                  goto rescan2;
              }

              
              asprintf(&value,"%d",inserted_choice);
              //value = inserted_choice + '0';
             
              if(prev_is_download == 1){
                asprintf(&temp_buf,"%d",num_pkt);
                strcpy(new_data_filename->sequence_number,temp_buf);
                prev_is_download = 0;
              }else{
                asprintf(&temp_buf,"%d",num_pkt);
                strcpy(new_data_filename->sequence_number,temp_buf);
              }

              
              fseek(file_upload, 0, SEEK_END);
              size = ftell(file_upload);
              asprintf(&size_file, "%ld",size);
              fseek(file_upload, 0, SEEK_SET);
              
              // ack per OK mandato dal server
            
              asprintf(&acknum, "%d", act_seq_num);
              strcpy(new_data_filename->ack_number, acknum);
              
              memset(new_data_filename->operation_no, 0,sizeof(new_data_filename->operation_no));
              strcpy(new_data_filename->operation_no, value);
              free(value);

              memset(new_data_filename->message, 0, MAXLINE);
              memcpy(new_data_filename->message, sendline, MAXLINE);

              strcpy(new_data_filename->size, size_file);

              asprintf(&new_port, "%d", port);
              strcpy(new_data_filename->port, new_port);
              num_pkt = num_pkt + (int)strlen(new_data_filename->message) + (int)strlen(new_data_filename->operation_no);
            
              oper_timer.sem_num = 0;
              oper_timer.sem_op = -1;
              oper_timer.sem_flg = 0;

signal45:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 411\n");
                 if(errno == EINTR){
                  goto signal45;
                 }
                 exit(-1);
              }

              ret_timer = srtSchedule();
#ifdef ADPTO
              append_timer_adaptive(ret_timer, new_data_filename->sequence_number, &list_head_tmr,new_data_filename->SYNbit,new_data_filename->ACKbit,new_data_filename->ack_number,new_data_filename->message,new_data_filename->operation_no,new_data_filename->FINbit,new_data_filename->port, new_data_filename->size, rto, rto, sample_value, 0);
#else
              append_timer(ret_timer, new_data_filename->sequence_number, &list_head_tmr,new_data_filename->SYNbit,new_data_filename->ACKbit,new_data_filename->ack_number,new_data_filename->message,new_data_filename->operation_no,new_data_filename->FINbit,new_data_filename->port, new_data_filename->size);              
#endif              
              
              append_sent_packets(new_data_filename->sequence_number, new_data_filename->SYNbit, new_data_filename->ACKbit, new_data_filename->ack_number,new_data_filename->message,new_data_filename->operation_no,new_data_filename->FINbit, new_data_filename->port, &list_head_sent_pkts, new_data_filename->size);
              
              loss_p = float_rand(0.0, 1.0);
              
              if(loss_p >= LOSS_PROBABILITY){
                if(sendto(sockfd, new_data_filename, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                  perror("errore in sendto 9");
                  exit(1);
                }
              }

              upload_filename = 1;
              flag_size_upload = 1;

              // non sincronizzo perché ancora non ho dato il permesso all'ack_upload di acoltare gli acks del server
              
              expected_ack = expected_ack + (int)strlen(new_data_filename->message) + (int)strlen(new_data_filename->operation_no);
              
              oper_timer.sem_num = 0;
              oper_timer.sem_op = 1;
              oper_timer.sem_flg = 0;

signal46:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                  perror("Error signaling 422\n");
                  if(errno == EINTR){
                    goto signal46;
                  }
                  exit(-1);
              }

              
              oper2.sem_num = 0;
              oper2.sem_op = 1;
              oper2.sem_flg = 0;

signal47:
              //signal
              if((semop(semid2,&oper2,1)) == -1){
                 perror("Error signaling 3\n");
                 if(errno == EINTR){
                    goto signal47;
                  }
                 exit(-1);
              }

              
              free(new_data_filename);
              
              // wait che sia stato ricevuto l'ack per il nome del file da parte dell'ack_upload_hanlder per inviare il contenuto al server
              oper_filename.sem_num = 0;
              oper_filename.sem_op = -1;
              oper_filename.sem_flg = 0;


rewait_filename:
              //wait
              if((semop(semid_filename, &oper_filename, 1)) == -1){
                 perror("Error waiting filename\n");
                 if(errno == EINTR){
                    errno = 0;
                    goto rewait_filename;
                 }
                 exit(-1);
              }

              flag_size_upload = 0;
              
              t_data *new_data_content = malloc(sizeof(t_data));
              if(new_data_content == NULL){
                  perror("\n malloc error");
                  exit(-1);
              }
              
              while(!feof(file_upload)){
                  fread(new_data_content->message, 1, sizeof(new_data_content->message), file_upload);
                  file_content++;
              }
              
              rewind(file_upload); // funzione per riposizionare il puntatore all'inizio del file
            
              while(!feof(file_upload)){

                  oper_window.sem_num = 0;
                  oper_window.sem_op = -1;
                  oper_window.sem_flg = 0;

rewindow:          
                 

                  if((semop(semid_window,&oper_window,1)) == -1){
                     if(errno == EINTR){
                       goto rewindow;
                     }
                     exit(-1);
                  }

                 
                    
                  memset(new_data_content->sequence_number, 0, sizeof(new_data_content->sequence_number));
                  
                  asprintf(&temp_buf,"%d",num_pkt);
                  strcpy(new_data_content->sequence_number,temp_buf);

                  fread(new_data_content->message, 1, sizeof(new_data_content->message), file_upload);

                  inserted_choice = 3;
                  
                  asprintf(&value,"%d",inserted_choice);
                  memset(new_data_content->operation_no, 0, sizeof(new_data_content->operation_no));
                  strcpy(new_data_content->operation_no, value);
                  free(value);
                  memset(new_data_content->port, 0, sizeof(new_data_content->port));
                  asprintf(&new_port, "%d", port);
                  strcpy(new_data_content->port, new_port);

                  num_pkt = num_pkt + (int)strlen(new_data_content->message) + (int)strlen(new_data_content->operation_no);


                  oper_timer.sem_num = 0;
                  oper_timer.sem_op = -1;
                  oper_timer.sem_flg = 0;

resignaling_433:
                  if((semop(semid_timer, &oper_timer, 1)) == -1){
                     if(errno == EINTR){
                      goto resignaling_433;
                     }
                     exit(-1);
                  }


                  oper_sum.sem_num = 0;
                  oper_sum.sem_op = -1;
                  oper_sum.sem_flg = 0;

resignaling455:
                  if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                    perror("Error signaling 455\n");
                    if(errno == EINTR){
                      goto resignaling455;
                    }
                     
                     exit(-1);
                  }

                  
                  ret_timer = srtSchedule();

#ifdef ADPTO
                  append_timer_adaptive(ret_timer, new_data_content->sequence_number, &list_head_tmr,new_data_content->SYNbit,new_data_content->ACKbit,new_data_content->ack_number,new_data_content->message,new_data_content->operation_no,new_data_content->FINbit,new_data_content->port, new_data_content->size, rto, rto, sample_value, 0);
#else
                  append_timer(ret_timer, new_data_content->sequence_number, &list_head_tmr,new_data_content->SYNbit,new_data_content->ACKbit,new_data_content->ack_number,new_data_content->message,new_data_content->operation_no,new_data_content->FINbit,new_data_content->port, new_data_content->size);                  
#endif                  
                  
                  append_sent_packets(new_data_content->sequence_number, new_data_content->SYNbit, new_data_content->ACKbit, new_data_content->ack_number,new_data_content->message,new_data_content->operation_no,new_data_content->FINbit, new_data_content->port, &list_head_sent_pkts, new_data_content->size);
                  
                  loss_p = float_rand(0.0, 1.0);
                  

                  if(loss_p >= LOSS_PROBABILITY){
                    if(sendto(sockfd, new_data_content, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                      perror("\nerrore in sendto 10");
                      exit(1);
                    }
                    
                    
                  }

                  

                  expected_ack = expected_ack + (int)strlen(new_data_content->message) + (int)strlen(new_data_content->operation_no);
                  
                  memset(new_data_content->message, 0, sizeof(new_data_content->message));
                  
                  oper_timer.sem_num = 0;
                  oper_timer.sem_op = 1;
                  oper_timer.sem_flg = 0;

signal50:
                  if((semop(semid_timer, &oper_timer, 1)) == -1){
                     perror("Error signaling 444\n");
                     if(errno == EINTR){
                      goto signal50;
                     }
                     exit(-1);
                  }

                  oper_sum.sem_num = 0;
                  oper_sum.sem_op = 1;
                  oper_sum.sem_flg = 0;

signal51:
                  if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                     perror("Error signaling 466\n");
                     if(errno == EINTR){
                      goto signal51;
                     }
                     exit(-1);
                  }

                  oper2.sem_num = 0;
                  oper2.sem_op = 1;
                  oper2.sem_flg = 0;

signal52:
                  if((semop(semid2,&oper2,1)) == -1){
                     perror("Error signaling 477\n");
                     if(errno == EINTR){
                      goto signal52;
                     }
                     exit(-1);
                  }

              }

              free(new_data_content);


              oper_window.sem_num = 0;
              oper_window.sem_op = -1;
              oper_window.sem_flg = 0;

rewindowx:
              
              if((semop(semid_window,&oper_window,1)) == -1){
                 perror("Error close 2\n");
                 if(errno == EINTR){
                  goto rewindowx;
                 }
                 exit(-1);
              }

              
              
              t_data *new_data_finish = malloc(sizeof(t_data));
              if(new_data_finish == NULL){
                  perror("\n malloc error");
                  exit(-1);
              }

              
              asprintf(&temp_buf,"%d",num_pkt);
              strcpy(new_data_finish->sequence_number,temp_buf);
              
              inserted_choice = 3;
            
              asprintf(&value,"%d",inserted_choice);
              memset(new_data_finish->operation_no, 0, sizeof(new_data_finish->operation_no));
              strcpy(new_data_finish->operation_no, value);
              free(value);

              asprintf(&new_port, "%d", port);
              strcpy(new_data_finish->port, new_port);
            
              memcpy(new_data_finish->message, "666", MAXLINE);
              num_pkt = num_pkt + (int)strlen(new_data_finish->message) + (int)strlen(new_data_finish->operation_no);

              oper_timer.sem_num = 0;
              oper_timer.sem_op = -1;
              oper_timer.sem_flg = 0;
rewait_t:

              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 488\n");
                 if(errno == EINTR){
                    goto rewait_t;
                 }
                 exit(-1);
              }

              oper_sum.sem_num = 0;
              oper_sum.sem_op = -1;
              oper_sum.sem_flg = 0;

signal53:
              if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                 perror("Error signaling 499\n");
                 if(errno == EINTR){
                  goto signal53;
                 }
                 exit(-1);
              }

              ret_timer = srtSchedule();
#ifdef ADPTO              
              append_timer_adaptive(ret_timer, new_data_finish->sequence_number, &list_head_tmr,new_data_finish->SYNbit,new_data_finish->ACKbit,new_data_finish->ack_number,new_data_finish->message,new_data_finish->operation_no,new_data_finish->FINbit,new_data_finish->port, new_data_finish->size, rto, rto, sample_value, 0);
#else
              append_timer(ret_timer, new_data_finish->sequence_number, &list_head_tmr,new_data_finish->SYNbit,new_data_finish->ACKbit,new_data_finish->ack_number,new_data_finish->message,new_data_finish->operation_no,new_data_finish->FINbit,new_data_finish->port, new_data_finish->size);
#endif              
              
              append_sent_packets(new_data_finish->sequence_number, new_data_finish->SYNbit, new_data_finish->ACKbit, new_data_finish->ack_number,new_data_finish->message,new_data_finish->operation_no,new_data_finish->FINbit, new_data_finish->port, &list_head_sent_pkts, new_data_finish->size);
              
              loss_p = float_rand(0.0, 1.0);
              
              if(loss_p >= LOSS_PROBABILITY){
                if(sendto(sockfd, new_data_finish, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                  perror("\nerrore in sendto 11");
                  exit(1);
                }
              }
             
              expected_ack = expected_ack + (int)strlen(new_data_finish->message) + (int)strlen(new_data_finish->operation_no);

              oper_timer.sem_num = 0;
              oper_timer.sem_op = 1;
              oper_timer.sem_flg = 0;

signal54:
              if((semop(semid_timer, &oper_timer, 1)) == -1){
                 perror("Error signaling 4111\n");
                 if(errno == EINTR){
                  goto signal54;
                 }
                 exit(-1);
              }

              oper_sum.sem_num = 0;
              oper_sum.sem_op = 1;
              oper_sum.sem_flg = 0;

signal55:
              if((semop(semid_sum_ack, &oper_sum, 1)) == -1){
                 perror("Error signaling 4222\n");
                 if(errno == EINTR){
                  goto signal55;
                 }
                 exit(-1);
              }

              oper2.sem_num = 0;
              oper2.sem_op = 1;
              oper2.sem_flg = 0;

signal56:
              if((semop(semid2,&oper2,1)) == -1){
                 perror("Error signaling 5\n");
                 if(errno == EINTR){
                  goto signal56;
                 }
                 exit(-1);
              }

              free(new_data_finish);
              
              oper1.sem_num = 0;
              oper1.sem_op = -1;
              oper1.sem_flg = 0;

rewait_semid:
              
              if((semop(semid,&oper1,1))== -1){
                if(errno == EINTR){
                  goto rewait_semid;
                }
                exit(-1);
              }

              if(all_ack_upload_received == 1){
                  
                  expected_next_seq_num--;

                  if(master_exists_flag == 1){
                    ret = timer_delete(master_IDTimer);                
                    if(ret == -1){
                      perror("\ntimer_delete error");
                    }

                    master_exists_flag = 0;
                  }

                  
                  sleep(4);
                  

                  memset((void *)&servaddr, 0, sizeof(servaddr)); 
                  // Filling server information 
                  servaddr.sin_family = AF_INET; 
                  servaddr.sin_port = htons(PORT); 
                  if(inet_aton(argv[1], &servaddr.sin_addr) <= 0){
                    fprintf(stderr, "errore in inet_aton\n");
                    exit(1);
                  }

                  printf("\n\nDo you want to go back to menu? \ndigit (y/n)..\n");
                  fflush(stdout);
                  all_ack_upload_received = 0;
                  prev_is_upload = 1;
                  file_not_found = 1;
                  enabler = 0;
                  retr_phase = 0;
                  packet_received = 0;
rescan6:
                  flush(stdin);
                  ret = scanf("%c", &cho);
                  
                  if(ret == EOF){
                    if(errno == EINTR){
                      goto reselect;
                    }
                  }
                  
                  if(ret != 0){
                    if(cho == 'y' || cho == 'Y'){
                        
                        system("clear");
                        choice_menu();
                        continue;
                        //goto reselect;
                    }else{
                        if(cho == 'n' || cho == 'N'){
                          ////////////////////////////////////////////////////////////////////////////////
                          // prima di fare exit devo comunicare al server che sto per chiudere, il server dovrà eliminarmi dalla 
                          // sua lista
                            exit(1);
                        }else{
                            flush(stdin);
                            printf("input not valid.. please reinsert y/n!\n");
                            
                            goto rescan6;
                        }
                    }
                  }else{
                    printf("no input.. please reinsert y/n!\n");
                    goto rescan6;
                  }

                  flush(stdin);
                  break;
              }else{


              }
              
              

          }

          if(act_ins_down == 1){

              if(client_closing == 1){
                  printf("signal CTRL+C sent\n");
                  fflush(stdout);
                  goto rerecv;
              }    
              //download
             
              second_part_download = 1;

              t_data *new_data = malloc(sizeof(t_data));
              if(new_data == NULL){
                  perror("malloc error");
                  exit(1);
              }

              inserted_choice = 2;

              asprintf(&value,"%d",inserted_choice);
              asprintf(&temp_buf,"%d",num_pkt);
              strcpy(new_data->sequence_number,temp_buf);

              memset(new_data->operation_no, 0, sizeof(new_data->operation_no));
              memset(new_data->message, 0, MAXLINE);
              strcpy(new_data->operation_no, "2\0");
              memcpy(new_data->message, sendline, MAXLINE);
              num_pkt = num_pkt+(int)strlen(new_data->message);
              free(value);
              
              servaddr1.sin_port = htons(porta_send_download_file);

              loss_p = float_rand(0.0, 1.0);
              
              if(loss_p >= LOSS_PROBABILITY){
                 if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr1, (socklen_t)sizeof(servaddr1)) < 0){
                    perror("errore in sendto 12");
                    exit(1);
                 }

              }
              
              
              expected_next_seq_num -= (int)strlen(sendline);
              
              ret_timer = srtSchedule();
#ifdef ADPTO
              append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
              append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);              
#endif
              
              expected_ack = expected_ack + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);

              free(new_data);
              
          }

          

        }else{

          switch(inserted_choice){
            
            case 1:
              printf("\n -------> List selected\n");
              memset(sendline, 0, MAXLINE);

              memset((void *)&servaddr, 0, sizeof(servaddr)); 
              // Filling server information 
              servaddr.sin_family = AF_INET; 
              servaddr.sin_port = htons(PORT); 
              if(inet_aton(argv[1], &servaddr.sin_addr) <= 0){
                fprintf(stderr, "errore in inet_aton\n");
                exit(1);
              }

              t_data *new_data_op1 = malloc(sizeof(t_data));
              if(new_data == NULL){
                  perror("malloc error");
                  exit(1);
              }

              //memcpy(sendline, choice, sizeof(choice));

              // valorizzo il campo sequence number
              asprintf(&temp_buf,"%d",num_pkt);
              strcpy(new_data_op1->sequence_number,temp_buf);

              // valorizzo il campo operation_no
              strcpy(new_data_op1->operation_no, "1\0");

              // valorizzo il campo messaggio
              memset(new_data_op1->message, 0, sizeof(new_data_op1->message));
              //memcpy(new_data->message, def_str, sizeof(def_str));

              strcpy(new_data_op1->message, "");

              memset(new_data_op1->size, 0, sizeof(new_data_op1->size));
              memset(new_data_op1->port, 0, sizeof(new_data_op1->port));
              memset(new_data_op1->FINbit, 0, sizeof(new_data_op1->FINbit));

              num_pkt = num_pkt + DEF_LENGTH;

              loss_p = float_rand(0.0, 1.0);

              if(loss_p >= LOSS_PROBABILITY){
                  
                  if(sendto(sockfd, new_data_op1, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                      perror("errore in sendto 12");
                      exit(1);
                  }
              }

              ret_timer = srtSchedule();
#ifdef ADPTO
              append_timer_adaptive(ret_timer, new_data_op1->sequence_number, &list_head_tmr,new_data_op1->SYNbit,new_data_op1->ACKbit,new_data_op1->ack_number,new_data_op1->message,new_data_op1->operation_no,new_data_op1->FINbit,new_data_op1->port, new_data_op1->size, rto, rto, sample_value, 0);
#else
              append_timer(ret_timer, new_data_op1->sequence_number, &list_head_tmr,new_data_op1->SYNbit,new_data_op1->ACKbit,new_data_op1->ack_number,new_data_op1->message,new_data_op1->operation_no,new_data_op1->FINbit,new_data_op1->port, new_data_op1->size);
#endif              
              
              prev_is_download = 0;
              expected_ack = expected_ack + (int)strlen(new_data_op1->message) + (int)strlen(new_data_op1->operation_no);
            
              free(new_data_op1);
               
              break;
               
            case 2:
              printf("\n -------> Download selected\n");


              // if switch_mean_d == 0 --> first request message of download, then 
              // if switch_mean_d == 1 --> ready to send name of the file to download
              
              if(switch_mean_d == 0){

                t_data *new_data1 = malloc(sizeof(t_data));
                if(new_data1 == NULL){
                    perror("malloc error");
                    exit(1);
                }

                memset(sendline, 0, MAXLINE);
                memcpy(sendline, choice, sizeof(choice));

                asprintf(&temp_buf,"%d",num_pkt);
                strcpy(new_data1->sequence_number,temp_buf);
                strcpy(new_data1->operation_no,"2\0");
                memcpy(new_data1->message, def_str, sizeof(def_str));
                num_pkt = num_pkt + DEF_LENGTH;
                handler_list_of_files = 0;

                loss_p = float_rand(0.0, 1.0);
                
               
                if(loss_p >= LOSS_PROBABILITY){
                    if(sendto(sockfd, new_data1, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                      perror("errore in sendto 13");
                      exit(1);
                    }
                }
                
                
                ret_timer = srtSchedule();
#ifdef ADPTO
                append_timer_adaptive(ret_timer, new_data1->sequence_number, &list_head_tmr,new_data1->SYNbit,new_data1->ACKbit,new_data1->ack_number,new_data1->message,new_data1->operation_no,new_data1->FINbit,new_data1->port, new_data1->size, rto, rto, sample_value, 0);
#else
                append_timer(ret_timer, new_data1->sequence_number, &list_head_tmr,new_data1->SYNbit,new_data1->ACKbit,new_data1->ack_number,new_data1->message,new_data1->operation_no,new_data1->FINbit,new_data1->port, new_data1->size);
#endif

                prev_is_download = 1;
                expected_ack = expected_ack + (int)strlen(new_data1->message) + (int)strlen(new_data1->operation_no);

                switch_mean_d = 1;
              }
              
              
              
              
              break;
                
            case 3:
              printf("\n -------> Upload selected\n");

              // if switch_mean_u == 0 --> first request message of upload, then 
              // if switch_mean_u == 1 --> ready to send name and content of the file to upload
              if(switch_mean_u == 0){

                  
                  t_data *new_data2 = malloc(sizeof(t_data));
                  if(new_data2 == NULL){
                      perror("malloc error");
                      exit(1);
                  }

                  //memset(sendline, 0, MAXLINE);
                  //strncpy(sendline, inserted_choice, strlen(choice));
                  //printf("SENDLINEEEE: %s\n", sendline);
                
                  asprintf(&value,"%d",inserted_choice);

                  asprintf(&temp_buf,"%d",num_pkt);
                  strcpy(new_data2->sequence_number,temp_buf);
                  
                  strcpy(new_data2->operation_no, value);
                  free(value);
                  
                  memcpy(new_data2->message, def_str, sizeof(def_str));

                  num_pkt = num_pkt + DEF_LENGTH;

                  loss_p = float_rand(0.0, 1.0);
                  
                 
                  if(loss_p >= LOSS_PROBABILITY){
                    if(sendto(sockfd, new_data2, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                      perror("errore in sendto 14");
                      exit(1);
                    }
                  }
                  
                 
                  ret_timer = srtSchedule();
#ifdef ADPTO
                  append_timer_adaptive(ret_timer, new_data2->sequence_number, &list_head_tmr,new_data2->SYNbit,new_data2->ACKbit,new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit,new_data2->port, new_data2->size, rto, rto, sample_value, 0);
#else
                  append_timer(ret_timer, new_data2->sequence_number, &list_head_tmr,new_data2->SYNbit,new_data2->ACKbit,new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit,new_data2->port, new_data2->size);                  
#endif
                  
                  expected_ack = expected_ack + (int)strlen(new_data2->message) + (int)strlen(new_data2->operation_no);

                  free(new_data2);
                
                  switch_mean_u = 1;
                  
                  
              }else{

                  
                  t_data *new_data = malloc(sizeof(t_data));
                  if(new_data == NULL){
                    perror("malloc error");
                    exit(1);
                  }

                  inserted_choice = atoi(sendline);
                  
                  asprintf(&value,"%d",inserted_choice);
                  asprintf(&temp_buf,"%d",num_pkt);
                  strcpy(new_data->sequence_number,temp_buf);
                  
                  strcpy(new_data->operation_no, value);
                  free(value);
                  
                  memcpy(new_data->message, def_str, sizeof(def_str));
                  
                  num_pkt = num_pkt + DEF_LENGTH;

                  loss_p = float_rand(0.0, 1.0);
                 
                  if(loss_p >= LOSS_PROBABILITY){
                    if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&servaddr, (socklen_t)sizeof(servaddr)) < 0){
                      perror("errore in sendto 15");
                      exit(1);
                    }

                  }

                  
                  ret_timer = srtSchedule();
#ifdef ADPTO    
                  append_timer_adaptive(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size, rto, rto, sample_value, 0);
#else
                  append_timer(ret_timer, new_data->sequence_number, &list_head_tmr,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port, new_data->size);                  
#endif

                  
                  expected_ack = expected_ack + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);

                  free(new_data);

              }
         
              break;
               
            case 4:
              printf("client quitting...\n");
              fflush(stdout);
              quit();

              break;
                
            default:
                
              printf("(1)WARNING: invalid choice.\nReinsert the option\n");
              fflush(stdout);
              goto rechoice;
    
          }

      }

     
    }
  
  }
  return 0;
} 
