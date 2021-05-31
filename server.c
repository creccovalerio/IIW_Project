// Server program 
#include <arpa/inet.h> 
#include <errno.h> 
#include <netinet/in.h> 
#include <signal.h> 
#include <stdio.h> 
#include <stdlib.h> 
#include <string.h> 
#include <sys/socket.h> 
#include <sys/types.h> 
#include <unistd.h> 
#include <sys/wait.h>
#include <sys/dir.h>
#include <dirent.h>
#include <pthread.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sem.h>
#include <time.h>
#include <unistd.h>
#include <stdint.h>
#include <semaphore.h>
#include <math.h>
#include <sched.h>

#define PORT 6010
#define MAXLINE 1445
#define FALSE 0;
#define TRUE !FALSE;
#define WIND_SIZE_T 10
#define WIND_SIZE_R 100
#define SEMKEYR 999
#define DEF_LENGHT 3
#define LOSS_PROBABILITY 0.05
#define MAXDIM 30
#define MIN_TIMEO 0.05
#define MAX_TIMEO 1.0
#define ALPHA 0.125
#define BETA 0.25



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

typedef struct timer{
  timer_t  idTimer;
  char SYNbit[2];
  char sequence_number[10];
  char ACKbit[2];
  char ack_number[10];
  char message[MAXLINE];
  char operation_no[2];
  char FINbit[2];
  int  sockfd;
  char port[6]; // porta sulla quale il client deve mandare l'ack
  char size[15];
#ifdef ADPTO
  float sample_value;
  float last_rto;
#endif
  struct timer *next;
  struct timer *prev;
}t_tmr;

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


typedef struct buffered_pkt{
  char sequence_number[10];
  char ack_number[10];
  char message[MAXLINE];
  char operation_no[2];
  struct buffered_pkt *next;
  struct buffered_pkt *prev; 
}t_buffered_pkt;

typedef struct timer_id{
	timer_t   tidp;
	timer_t   id;
	int 	  sockfd;
	int 	  master_enabler;
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
	struct timer_id *next;
	struct timer_id *prev;
}t_timid;

typedef struct descriptor{
	int ds;
	struct descriptor *next;
	struct descriptor *prev;
}t_d;


typedef struct client_info{
	int 		    sockfd;
	unsigned short	port;
	int 			rand_c;
	char 			ipaddr[20];
	int 			expected_next_seq_number;
	int 			sequence_number;
	int  			ack_number;
	int 			last_ack;
	int 			download_list_phase;
	int 			port_download;
	int 			sock_filename_download;
	int 			sock_list;
	int 			sock_download;
	int 			retr_phase;
	int		        timerid;
	int			    operation_value;
	int 			semid_shared_queue;  //semaforo per abilitare lettura area memoria condivisa per ritrasmissione
	int 			ack_received; // number of acks to receive
	int 			semid_client; // semaforo sul quale fare signal/wait per gli acks (signal ogni volta che mando un pck per abilitare il thread figlio per gestire acks che si mangerà il gettone)
	int 			semid_expected; // semaforo per gestione del valore di expected_next_seq_num
	int             semid_retr_turn; 
	int 			semid_timer; //semaforo per creare id timers univoci
	int 			semid_sum_ack;  //semaforo per evitare problemi sincro su ultimo valore ack duplicati
	int 			semid_upclosing; // semaforo per gestire la chiusura dell'upload con interrupt software
	int 			semid_window; // semaforo per gestire la finestra di invio
	pthread_mutex_t lock;
	pthread_mutex_t lock2;
	pthread_cond_t  condition;
	int 			thread_retr_fin;
	int 			thread_retr_ord;
	timer_t 		shared_tim;
	int 			receive_window; // capienza del buffer di ogni client per ricevere pacchetti
	int 			sum_ack; //valore nel caso di ack duplicati  //mi indica l'eventuale ricezione di ack cumulativi
	int 			semid_fileno;  //semaforo per assicurarci che numero files/nome file da gestire per il client sia sempre ricevuto come primo pkt (used in list and download)
	int 			first_pkt_sent;
	int             master_timer;  //flag per creazione timer di controllo globale
	timer_t 		master_IDTimer;  // ID del timer master da eliminare alla fine della list_files
	int  			master_exists_flag; // flag per verificare eesistenza master timer
	int 			receiving_ack_phase; 
	int 			receiving_ack_phase_download;
	int 			num_pkt_buff;
	int 			flag_last_ack_sent;  // flag per abilitare la creazione del timer da 16 sec
	int 			flag_upload_wait;
	int 			closing_ack; // flag per abilitare la chiusura con interrupt lato server
	int 			file_content; // variabile per permettere la chiusura dell'ack_download_handler nel momento in cui riceviamo tutti gli acks attesi per il contenuto del file  
	int 			upload_closing; // flag per abiliare chiusura upload;
	t_buffered_pkt  *buff_pkt_client;  // puntatore alla n-ennesima lista dei pacchetti bufferizzati di un client
	t_tmr           *timer_list_client; // puntatore alla lista dei timer dello specifo client
	t_timid 		*timer_id_list_client; // puntatore alla lista dei timid timer dello specifico client
	t_sent_pkt      *list_sent_pkt;  //puntatore alla lista dei pkt inviati per ogni client
	pthread_t 		upload_tid;  // upload thread ID 
	int 			var_req; // variabile per gestire richiesta upload
	int             hs;  //variabile abilitata solo in fae di handshake
	long int 		file_size; // dimensione del file di upload che il client decide di caricare presso il server
	int 			rand_hs;  //campo che serve per salvare numero randomico per hs
	int 			flag_close;   //campo abilitato a zero quando il client mi manda il finbit
	int             second_flag_close; //campo abilitato quando scaduto il timer il client non mi contatta e quindi posso inviare la mia seq di chiusura
	int             semid_last_timer; //timer per abilitare il fatto che il client ha ricevuto il mio ack per il suo finbit
	int 			flag_server_close; // flag per abilitare chiusura finale del server
	int 			estab_s; // variabile per impedire che si gestiscano cose dopo aver fatto estab server
	int    			filename_bytes; //variabile che permette di popolare il campo size all'interno della handling timeout
	int 			ok_phase;
	int 			create_file;
	pthread_t 		retrasmission_thread;
	int 			scount; //contatore per girare sulla coda di ritrasmissione
#ifdef ADPTO
	float 			rto;
	float 			rto_sec;
	float 			rto_nsec;
	float 			sample_rtt;
	float 			estimated_rtt;
	float 			dev;
	int 			first_to; // flag usato per impostare la deviance a sample_rtt/2 se primo giro
#endif
	struct 			client_info *next;
	struct 			client_info *prev; 
}t_client;

void *handling_timeout_retransmission(void *packet);
void *retrasmission_thread(void *client);
void handling_master_timer(int sockfd, timer_t tidp, t_tmr** list_head_global_tmr);
void delete(unsigned short num_port, char* ip_address, t_client **list_head_ptr, t_d **list_head_ptr_des);
void print_list(t_client *list_head_ptr);
void print_list_des(t_d *list_head_ptr);
void delete_timer(timer_t idT, int sockfd, t_tmr **list_head_ptr_tmrs);
void delete_id_timer(timer_t tidp, int sockfd, t_timid **list_head_ptr_id_tmrs);


int 			num = 0;
int threads = 0;
int          	udpfd;
int 		 	i = 0;
int 			count_client = 0;
int 			count_descriptor = 0;
int 			cont = 0;
struct 			sockaddr_in cliaddr; 
struct 	    	sockaddr_in servaddr;
pthread_mutex_t	up_lock;
int 			semid1,con_close=0;
int 			final_counter,final_counter2;
int 			bypass = 0;
t_client 		*list_head = NULL;
t_d             *list_head_d = NULL;
char			buffer[MAXLINE];
char			buffer1[MAXLINE];
char			path[MAXLINE];
char 			def_str[3] = "404"; // default string for file not found in the server
char			def_str2[3] = "666"; // default string for finish send file content
int     		sigNo = 34; // SIGRTIM value
int     		expected_ack = 0;
int 			k = 0;
t_data 			*new_data_gl_list;
t_data 			*new_data_gl_upload;
t_data 			*new_data_gl_download;
pthread_t 		tid_ack_list;
int 			semid_server_closure; // semaforo per eliminare un singolo client in fase di chiusura del server


union semun{
	int val;
	struct semid_ds *buf;
	unsigned short *array;
	struct sem_info *_buf;
};


int max(int udpfd) 
{ 

	int temp_max = 0;
	t_d *n;

	for (n=list_head_d; n!=NULL; n=n->next){
	 	 if(n->ds > temp_max){
	 	 	temp_max = n->ds;
	 	 }
	}
	
	if(temp_max > udpfd){
		return temp_max;
	}else{
		temp_max = udpfd;
		return temp_max;
	}
	
} 

int file_select(struct direct *entry){

	if((strcmp(entry->d_name, ".") == 0) || (strcmp(entry->d_name, "..") == 0)){
		return FALSE;
	}else{
		return TRUE;
	}
}


// funzione per aggiungere la socket creata per il client
void append_queue_descriptors(int sockfd, t_d **list_head_ptr){
	
	t_d *new_node = malloc(sizeof(t_d));
	
	if (new_node == NULL){
		perror("malloc error!\n");
		exit(1);
	}

	
	new_node->ds = sockfd;
	new_node->next = NULL;
	new_node->prev = NULL;

	
	/* se la lista è vuota append in testa*/
	if (*list_head_ptr == NULL){
		// nuovo descrittore inserito
		*list_head_ptr = new_node;
		list_head_d = *list_head_ptr;
		return;
	}

	t_d *n = *list_head_ptr;
	/* scorro la lista finché non trovo che il next del nodo è NULL per inserire in coda*/
	while (n->next != NULL){
		if(n->ds != sockfd){
			n=n->next;	
		}else{
			// descrittore già inserito
			return;		
		}
		
	}

	if(n->ds != sockfd){
		// nuovo descrittore inserito in coda
		n->next = new_node;
		new_node->prev = n;
		count_descriptor++;
	}else{
		// descrittore già inserito
		return;
	}
	

}

// funzione per aggiungere un nuovo client da servire alla lista dei client correntemente collegati al server
void append_queue(unsigned short num_port, char* ip_address, int num, t_client **list_head_ptr, t_d **list_head_ptr_des){

	t_client *new_node = malloc(sizeof(t_client));
	char buffer[20];
	int sock_d;
	int rc;
	int id_sem, id_sem2, id_sem4, id_sem5,id_sem6,id_sem7, id_sem8, id_sem9, id_sem10,id_sem11;
	
	if (new_node == NULL){
		perror("\n malloc error");
		exit(1);
	}

	//semaforo per attivare thread degli ack sia di lista che di download
	id_sem = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem, 0, SETVAL, 0) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	id_sem2 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem2 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem2, 0, SETVAL, 0) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem2, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	
  	id_sem4 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem4 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem4, 0, SETVAL, 1) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem4, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	id_sem5 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem5 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem5, 0, SETVAL, 1) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem5, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	id_sem6 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem6 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem6, 0, SETVAL, 1) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem6, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	id_sem7 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem7 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem7, 0, SETVAL, 0) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem7, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	id_sem8 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem8 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem8, 0, SETVAL, 0) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem8, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	id_sem9 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem9 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem9, 0, SETVAL, 0) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem9, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	id_sem10 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem10 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(id_sem10, 0, SETVAL, WIND_SIZE_T) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem10, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	id_sem11 = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(id_sem11 == -1){
		perror("\nsemget error");
		exit(-1);
	}

	//semaforo per abilitare il thread di ritrasmissione a leggere la coda
	if(semctl(id_sem11, 0, SETVAL, 0) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(id_sem11, -1, IPC_RMID, NULL);
    	exit(-1);
  	}

  	pthread_mutex_init(&new_node->lock, NULL);
  	pthread_mutex_init(&new_node->lock2, NULL);
  	pthread_mutex_lock(&new_node->lock2);
  	pthread_cond_init(&new_node->condition, NULL);

	memset(buffer, 0, 20);
	strcpy(buffer, ip_address);
	new_node->port = num_port;
	strcpy(new_node->ipaddr,buffer);
	new_node->sequence_number = 0;
	new_node->ack_number = 0;
	new_node->last_ack = 0;
	new_node->expected_next_seq_number = 0;
	new_node->download_list_phase = 0;
	new_node->retr_phase = 0; 
	new_node->timerid = 0;
	new_node->sum_ack = 0;
	new_node->ack_received = 0;
	new_node->semid_client = id_sem;
	new_node->semid_expected = id_sem2;
	new_node->semid_timer = id_sem4;
	new_node->semid_retr_turn = id_sem5;
	new_node->semid_sum_ack = id_sem6;
	new_node->semid_fileno = id_sem7;
	new_node->semid_upclosing = id_sem8;
	new_node->semid_last_timer = id_sem9;
	new_node->semid_window = id_sem10;
	new_node->semid_shared_queue = id_sem11;
	new_node->receive_window = WIND_SIZE_R;
	new_node->receiving_ack_phase = 0;
	new_node->receiving_ack_phase_download = 0;
	new_node->thread_retr_fin = 0;
	new_node->thread_retr_ord = 0;
	new_node->first_pkt_sent = 0;
	new_node->master_timer = 0;
	new_node->master_IDTimer = 0;
	new_node->master_exists_flag = 0;
	new_node->file_content = 0;
	new_node->num_pkt_buff = 0;
	new_node->flag_last_ack_sent = 0;
	new_node->flag_upload_wait = 0;
	new_node->closing_ack = 0;
	new_node->upload_closing = 0;
	new_node->var_req = 0;
	new_node->file_size = 0;
	new_node->buff_pkt_client = NULL;
	new_node->timer_list_client = NULL;
	new_node->timer_id_list_client = NULL;
	new_node->list_sent_pkt = NULL;
	new_node->rand_hs = num;
	new_node->hs = 1;
	new_node->operation_value = 0;
	new_node->estab_s = 0;
	new_node->flag_close = 0;
	new_node->second_flag_close = 0;
	new_node->flag_server_close = 0;
	new_node->filename_bytes = 0;
	new_node->ok_phase = 1;
	new_node->create_file = 0;
	new_node->upload_tid = NULL;
	new_node->scount = 0;

	

	pthread_attr_t attr;
	struct sched_param param;
	if((rc = pthread_attr_init(&attr)) != 0){
		perror("\n pthread_attr_init error");
		exit(-1);
	}

	rc = pthread_attr_getschedparam(&attr, &param);
	(param.sched_priority) = 20;
	rc = pthread_attr_setschedparam(&attr, &param);


  	// thread per gestione ritrasmissione
  	int ret = pthread_create(&new_node->retrasmission_thread, &attr, retrasmission_thread, ((void *)new_node));
  	if(ret != 0){
  		perror("\npthread_create error in retransmission");
  		exit(-1);
  	}



#ifdef ADPTO
	new_node->rto = 0.5;
	new_node->estimated_rtt = 0.5;
	new_node->sample_rtt = 0.0;
	new_node->dev = 0.0;
#endif
	new_node->next = NULL;
	new_node->prev = NULL;

	
	/* se la lista è vuota append in testa*/
	if (*list_head_ptr == NULL){

		sock_d = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock_d < 0) { 
			perror("errore in socket");
			exit(1);
		}

		// binding server addr structure to udp sockfd 
		bind(sock_d, (struct sockaddr*)&servaddr, sizeof(servaddr));

	 	append_queue_descriptors(sock_d, list_head_ptr_des);
	 	new_node->sockfd = sock_d;
		*list_head_ptr = new_node;
		list_head = *list_head_ptr;
		count_client++;
		printf("\nClient %s dalla porta: %hu ADDED --> socket: %d\n", ip_address, num_port, sock_d);
		
		return;
	}

	t_client *n = *list_head_ptr;
	/* scorro la lista finché non trovo che il next del nodo è NULL per inserire in coda*/
	while (n->next != NULL){
		if(n->port != num_port){
			n=n->next;	
		}else{
			if(strcmp(n->ipaddr, ip_address) != 0){
				n=n->next;
			}else{
				// client già inserito
				return;
			}
			
		}
		
	}

	if(n->port != num_port){
		sock_d = socket(AF_INET, SOCK_DGRAM, 0);
		if (sock_d < 0) { 
    		perror("errore in socket");
    		exit(1);
  		}

		// binding server addr structure to udp sockfd 
		bind(sock_d, (struct sockaddr*)&servaddr, sizeof(servaddr));
		
	 	append_queue_descriptors(sock_d, list_head_ptr_des);
		new_node->sockfd = sock_d;
		n->next = new_node;
		new_node->prev = n;
		count_client++;
		printf("\n Client %s dalla porta: %hu ADDED --> socket: %d\n", ip_address, num_port, sock_d);
	

		
	}else{
		if(strcmp(n->ipaddr, ip_address) != 0){
			sock_d = socket(AF_INET, SOCK_DGRAM, 0);
			if (sock_d < 0) { 
    			perror("errore in socket");
    			exit(1);
  			}

			// binding server addr structure to udp sockfd 
			memset((void *)&servaddr, 0, sizeof(servaddr));
		    servaddr.sin_family = AF_INET; 
		    servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
		    servaddr.sin_port = htons(0); 
			bind(sock_d, (struct sockaddr*)&servaddr, sizeof(servaddr));
	 		
	 	
	 		append_queue_descriptors(sock_d, list_head_ptr_des);
			new_node->sockfd = sock_d;
			n->next = new_node;
			new_node->prev = n;
			count_client++;
			printf("\n Client %s dalla porta: %hu ADDED --> socket: %d\n", ip_address, num_port, sock_d);

			
		}else{
			// client già inserito
			return;
		}
	}
	

}


//funzione per appendere i pacchetti inviati ed eventualmente da ritrasmettere in caso di tre ack duplicati
void append_sent_packets(char *seq_num ,char *synbit, char* ackbit, char *acknum, char*msg, char* op_no, char* finbit, int sock_fd, char *port, t_sent_pkt **list_head_ptr_sent_pkts,char *size){
	

	  t_client *dest_client;	
	  
	  for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
	    if(dest_client->sockfd == sock_fd){
		    break;
	    }  
	  }	

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
	  new_node->sockfd = sock_fd;
	  new_node->next = NULL;
	  new_node->prev = NULL;

	  
	  /* se la lista è vuota append in testa*/
	  if (*list_head_ptr_sent_pkts == NULL){
	     *list_head_ptr_sent_pkts = new_node;
	     dest_client->list_sent_pkt = *list_head_ptr_sent_pkts;
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

// funzione per salvare le informazioni del timer creato per inviare un determinato pacchetto.. vengono salvate anche le sue informazioni per la ritrasmissione (caso timer adattivi)
#ifdef ADPTO
void append_timer_adaptive(timer_t idT, char *seq_num, t_tmr **list_head_ptr_timers,char *synbit,char* ackbit,char *acknum,char*msg,char* op_no,char* finbit,int sock_fd, char *port,char *size, float rto_at, float last_rto_at, float sample_value_at, int retr){

  t_client *dest_client;	
  for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
    if(dest_client->sockfd == sock_fd){
	    break;
    }  
  }

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
  memcpy(new_node->message, msg,MAXLINE);
  strcpy(new_node->operation_no,op_no);
  strcpy(new_node->FINbit, finbit);
  strcpy(new_node->port, port);
  strcpy(new_node->size, size);
  new_node->sockfd  = sock_fd;
  new_node->last_rto = rto_at;
  if(retr == 0){
  	new_node->sample_value = 0.0;
  }
  else{
  	new_node->sample_value = last_rto_at + sample_value_at; 
  }
  
  new_node->next = NULL;
  new_node->prev = NULL;

  
  /* se la lista è vuota append in testa*/
  if (*list_head_ptr_timers == NULL){
     *list_head_ptr_timers = new_node;
     dest_client->timer_list_client = *list_head_ptr_timers;
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


// funzione per salvare le informazioni del timer creato per inviare un determinato pacchetto.. vengono salvate anche le sue informazioni per la ritrasmissione
void append_timer(timer_t idT, char *seq_num, t_tmr **list_head_ptr_timers,char *synbit,char* ackbit,char *acknum,char*msg,char* op_no,char* finbit,int sock_fd, char *port,char *size){

  t_client *dest_client;	
  for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
    if(dest_client->sockfd == sock_fd){
	    break;
    }  
  }

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
  memcpy(new_node->message, msg,MAXLINE);
  strcpy(new_node->operation_no, op_no);
  strcpy(new_node->FINbit, finbit);
  strcpy(new_node->port, port);
  strcpy(new_node->size, size);
  new_node->sockfd  = sock_fd;
  new_node->next = NULL;
  new_node->prev = NULL;

  
  /* se la lista è vuota append in testa*/
  if (*list_head_ptr_timers == NULL){  
     *list_head_ptr_timers = new_node;
     dest_client->timer_list_client = *list_head_ptr_timers;
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

// funzione per allocare l'area di memoria prevista a cui far puntare la sival_ptr in caso scatti il timer
t_timid* append_id_timer(timer_t timerid, int sockfd, int master_enabler, t_timid **list_head_ptr_id_timers){
  
  t_client *dest_client;
  for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
    if(dest_client->sockfd == sockfd){
	    break;
    }  
  }	

  t_timid *new_node = malloc(sizeof(t_timid));
  
  if (new_node == NULL){
    printf("malloc error!\n");
    exit(1);
  }

  
  new_node->id = (timer_t)timerid;
  new_node->sockfd  = sockfd;
  new_node->master_enabler = master_enabler;
  new_node->next = NULL;
  new_node->prev = NULL;

  
  /* se la lista è vuota append in testa*/
  if (*list_head_ptr_id_timers == NULL){
     *list_head_ptr_id_timers = new_node;
     dest_client->timer_id_list_client = *list_head_ptr_id_timers;
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

// funzione per inserire l'effettivo id del timer nella struct che verrà puntata dalla sival_ptr
void append_real_idTimer(timer_t timerid, timer_t tidp, int sockfd,char *sequence_number,char *SYNbit,char *ACKbit,char *ack_number,char *message,char *operation_no,char *FINbit, char *port,char *size, t_timid **list_head_ptr_id_timers){

	t_timid *previous;
	t_timid *current = *list_head_ptr_id_timers;

	if((current->id == timerid) && (current->sockfd == sockfd)){
    	current->tidp = tidp;
		strcpy(current->sequence_number, sequence_number);
		strcpy(current->SYNbit,SYNbit);
		strcpy(current->ACKbit,ACKbit);
		strcpy(current->ack_number,ack_number);
		memcpy(current->message, message,MAXLINE);
		strcpy(current->operation_no, operation_no);
		strcpy(current->FINbit, FINbit);
		strcpy(current->port, port);
		strcpy(current->size, size);

    	return;
    }

	while (current != NULL){
	 	if((current->id == timerid) && (current->sockfd == sockfd)){
	 		current->tidp = tidp;
	 		strcpy(current->sequence_number, sequence_number);
			strcpy(current->SYNbit,SYNbit);
			strcpy(current->ACKbit,ACKbit);
			strcpy(current->ack_number,ack_number);
			memcpy(current->message, message,MAXLINE);
			strcpy(current->operation_no, operation_no);
			strcpy(current->FINbit, FINbit);
			strcpy(current->port, port);
			strcpy(current->size, size);

	 		break;
	 	}
	 	else{
	 		previous = current;
	 	   	current = current->next;
	 	}	 	   
	}
}

void append_real_idTimerS(timer_t timerid, timer_t tidp, int sockfd,t_timid **list_head_ptr_id_timers){

	t_timid *previous;
	t_timid *current = *list_head_ptr_id_timers;

	if((current->id == timerid) && (current->sockfd == sockfd)){
    	current->tidp = tidp;

    	return;
    }

	while (current != NULL){
	 	if((current->id == timerid) && (current->sockfd == sockfd)){
	 		current->tidp = tidp;
	 		

	 		break;
	 	}
	 	else{
	 		previous = current;
	 	   	current = current->next;
	 	}	 	   
	}
}

// funzione per bufferizzare i pacchetti arrivati fuori sequenza
void append_buffered_packet(char *seq_num, char *acknum, char *message, char* op_no, int sockfd, t_buffered_pkt **list_head_ptr_buffered_pkts){

  t_client *dest_client;

  for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
        if(dest_client->sockfd == sockfd){
		    break;
        }  
  }

  t_buffered_pkt *new_node = malloc(sizeof(t_buffered_pkt));
  if(new_node == NULL){
  	perror("\n malloc error");
  	exit(-1);
  }

  t_buffered_pkt *current;
  t_buffered_pkt *previous;

  current = *list_head_ptr_buffered_pkts;

  strcpy(new_node->sequence_number, seq_num);
  strcpy(new_node->ack_number,acknum);
  memcpy(new_node->message, message, MAXLINE);
  strcpy(new_node->operation_no,op_no);
  new_node->next = NULL;
  new_node->prev = NULL;

  
  /* se la lista è vuota append in testa*/
  if (*list_head_ptr_buffered_pkts == NULL){
     *list_head_ptr_buffered_pkts = new_node;
     dest_client->buff_pkt_client = *list_head_ptr_buffered_pkts;
     return;
  }


  while (current != NULL){
      if((strcmp(message, current->message) == 0) && (atoi(seq_num) == atoi(current->sequence_number))){
         // scarto il pacchetto perché è un duplicato --> già bufferizzato
         dest_client->receive_window++;
         return;
      }     
      
      previous = current;
      current = current->next;
  
  }

  previous->next = new_node;
  new_node->next = current;
  
}

float float_rand( float min, float max )
{
    float scale = rand() / (float) RAND_MAX; /* [0, 1.0] */
    return min + scale * ( max - min );      /* [min, max] */
}

void print_list_timer(t_tmr *list_head_ptr){

   t_tmr *n;

   printf("\nLista collegata: ");
   for (n=list_head_ptr;n!=NULL;n=n->next){
      printf("\nTimer--> SEQ_NUM: %s", n->sequence_number);
   }
}


// funzione per andare a creare un'istanza di un timer da generare nel momento dell'invio di un pacchetto
static timer_t makeTimer(int sockfd, int timerid,char *sequence_number,char *SYNbit,char *ACKbit,char *ack_number,char *message,char *operation_no,char *FINbit, char *port,char *size)
{
    struct sigevent    te;
    struct itimerspec  its;
    timerid++;	
    t_client* tmp;
    int ret;
    int master_enabler = 0;

    for (tmp=list_head; tmp!=NULL; tmp=tmp->next){
		if(tmp->sockfd == sockfd){
	 		// devo fare retrieve valore master timer
	 		if(tmp->master_timer == 1){
	 			master_enabler = 1;
	 		}
#ifdef ADPTO
	 		val_dec = modff(tmp->rto, &tmp->rto_sec);
	 		tmp->rto_nsec = (tmp->rto - tmp->rto_sec)*pow(10,9);
#endif
			break;
		}	
	}

    /* Set and enable alarm */
    te.sigev_notify = SIGEV_SIGNAL;
    te.sigev_signo = sigNo;
    te.sigev_value.sival_ptr = append_id_timer((timer_t)timerid, sockfd, master_enabler, &tmp->timer_id_list_client);

    //così facendo si spera che sival punta sempre ad area di memoria differente
    timer_t tidp;
    
    if(timer_create(CLOCK_REALTIME, &te, &tidp) != 0){
      perror("\n timer_create ERROR (1)");
    }

    append_real_idTimer((timer_t)timerid, tidp, sockfd, sequence_number, SYNbit, ACKbit, ack_number, message, operation_no, FINbit, port, size, &tmp->timer_id_list_client);


	if(tmp->master_timer == 1){
		//imposto timer lungo di controllo
		tmp->master_IDTimer = tidp;

		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 1000;
		its.it_value.tv_nsec = 0;
	}
	else if(tmp->flag_last_ack_sent == 1) {
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 4;
		its.it_value.tv_nsec = 0;
	}else if(tmp->flag_upload_wait == 1){
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 16;
		its.it_value.tv_nsec = 0;
	}else{
#ifdef ADPTO
		its.it_interval.tv_sec = 0;
	    its.it_interval.tv_nsec = 0;
	    its.it_value.tv_sec = tmp->rto_sec;
	    its.it_value.tv_nsec = tmp->rto_nsec;
#else
	    its.it_interval.tv_sec = 0.0;
	    its.it_interval.tv_nsec = 0.0;
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

static timer_t makeTimerS(int sockfd, int timerid)
{
    struct sigevent    te;
    struct itimerspec  its;
    timerid++;	
    t_client* tmp;
    int ret;
    int master_enabler = 0;

    for (tmp=list_head; tmp!=NULL; tmp=tmp->next){
		if(tmp->sockfd == sockfd){
	 		// devo fare retrieve valore master timer
	 		if(tmp->master_timer == 1){
	 			master_enabler = 1;
	 		}
#ifdef ADPTO
	 		val_dec = modff(tmp->rto, &tmp->rto_sec);
	 		tmp->rto_nsec = (tmp->rto - tmp->rto_sec)*pow(10,9);
#endif
			break;
		}	
	}

    /* Set and enable alarm */
    te.sigev_notify = SIGEV_SIGNAL;
    te.sigev_signo = sigNo;
    te.sigev_value.sival_ptr = append_id_timer((timer_t)timerid, sockfd, master_enabler, &tmp->timer_id_list_client);

    //così facendo si spera che sival punta sempre ad area di memoria differente
    timer_t tidp;
    
    if(timer_create(CLOCK_REALTIME,&te, &tidp) != 0){
      perror("\n timer_create ERROR (2)");
    }

    append_real_idTimerS((timer_t)timerid, tidp, sockfd,&tmp->timer_id_list_client);


	if(tmp->master_timer == 1){
		//imposto timer lungo di controllo
		tmp->master_IDTimer = tidp;

		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 1000;
		its.it_value.tv_nsec = 0;
	}
	else if(tmp->flag_last_ack_sent == 1) {
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 4;
		its.it_value.tv_nsec = 0;
	}else if(tmp->flag_upload_wait == 1){
		its.it_interval.tv_sec = 0;
		its.it_interval.tv_nsec = 0;
		its.it_value.tv_sec = 16;
		its.it_value.tv_nsec = 0;
	}else{
#ifdef ADPTO
		its.it_interval.tv_sec = 0;
	    its.it_interval.tv_nsec = 0;
	    its.it_value.tv_sec = tmp->rto_sec;
	    its.it_value.tv_nsec = tmp->rto_nsec;
#else
	    its.it_interval.tv_sec = 0;
	    its.it_interval.tv_nsec = 0;
	    its.it_value.tv_sec = 0.0;
	    its.it_value.tv_nsec = 0.6*pow(10,9);
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


// thread in ascolto del segnale di SIGRTMIN che ha lo scopo di segnale il thread per la ritrasmissione di un pacchetto
void *signal_handler_thread(){

	sigset_t           set;
	t_client      	   *tmp;
    struct sembuf      oper_timer;
    struct sembuf      oper_delete;
    int                ret;
  	pthread_t          tid_retr;
    int 		       find2 = 0;
	siginfo_t 	  	   info;
	t_timid 	       *tmr;

	sigemptyset(&set);
	sigaddset(&set, SIGRTMIN);

	while(1){

		sigwaitinfo(&set, &info);

		tmr = (&info)->si_value.sival_ptr;
		
		tmp = NULL;
		find2 = 0;


		for(tmp = list_head; tmp != NULL; tmp = tmp->next){
			
    	// find the client to forward the retrasmission
	    	if(tmp->sockfd == tmr->sockfd){
	    		find2 = 1;
			    break;
	    	}        	
	    }

	    if(find2 == 0){
			continue;
	    }


		if((tmr)->master_enabler == 1){
		    handling_master_timer((tmr)->sockfd, (tmr)->tidp, &tmp->timer_list_client);
		  	continue;
		}

		if(tmp->flag_server_close == 1){
		  	
	  		oper_delete.sem_num = 0;
			oper_delete.sem_op = -1;
			oper_delete.sem_flg = 0;
			  	

rewaiting_last_timer:  
			
			//wait su lista timer
			if((semop(semid_server_closure, &oper_delete, 1)) == -1){
				perror("Error rewaiting last timer \n");
				if(errno == EINTR){
					goto rewaiting_last_timer;
				}
				exit(-1);
			}

			delete(tmp->port, tmp->ipaddr, &list_head, &list_head_d);
			print_list(list_head);
	    	print_list_des(list_head_d);

			//se tutti mi rispondon mando msg a tutti
			if(count_client == 0){
				//chiusura
				printf("\nSERVER CLOSED.. Bye");
				exit(0);
			}

	  	 	oper_delete.sem_num = 0;
		  	oper_delete.sem_op = 1;
		  	oper_delete.sem_flg = 0;

resignal_server_close:

		  	if((semop(semid_server_closure, &oper_delete, 1)) == -1){
		     	perror("Error waiting 3\n");
		     	if(errno == EINTR){
		     		goto resignal_server_close;
		     	}
		     	exit(-1);
		  	}

		  	continue;

		}

        if(tmp->flag_close == 1 || tmp->second_flag_close == 1){
          	// thread per gestione ritrasmissione
          	ret = pthread_create(&tid_retr, NULL, handling_timeout_retransmission, ((void *)tmr));
          	if(ret != 0){
          		perror("\npthread_create error in retransmission");
          		exit(-1);
          	}

          	continue;
        }

        switch(tmp->operation_value){

        	case 0:
        		
        		pthread_mutex_lock(&tmp->lock);

        		// retrieve del pacchetto da ritrasmettere
        		tmp->shared_tim = tmr->tidp;

        		tmp->thread_retr_fin = 1;

        		//signal per sbloccare il thread di ritrasmissione
        		if((ret = pthread_cond_signal(&tmp->condition)) != 0){
	        		printf("\npthread_cond_signal error -err code: %d-", ret);
	        		exit(-1);
	        	}
        		
        		if((ret = pthread_mutex_unlock(&tmp->lock)) != 0){
        			printf("\n(1)pthread_mutex_unlock error - err code: %d", ret);
        			exit(-1);
        		}
        		
        		while(1){
        			if(tmp->thread_retr_ord == 1){
        				tmp->thread_retr_ord = 0;
        				break;
        			}
        		}


        		break;
              	


          	case 1:
              	
              	// thread per gestione ritrasmissione
              	pthread_mutex_lock(&tmp->lock);

        		// retrieve del pacchetto da ritrasmettere
        		tmp->shared_tim = tmr->tidp;

        		tmp->thread_retr_fin = 1;

        		//signal per sbloccare il thread di ritrasmissione
        		if((ret = pthread_cond_signal(&tmp->condition)) != 0){
	        		printf("\npthread_cond_signal error -err code: %d-", ret);
	        		exit(-1);
	        	}
        		
        		if((ret = pthread_mutex_unlock(&tmp->lock)) != 0){
        			printf("\n(1)pthread_mutex_unlock error - err code: %d", ret);
        			exit(-1);
        		}
        		
        	
        		while(1){
        			if(tmp->thread_retr_ord == 1){
        				tmp->thread_retr_ord = 0;
        				break;
        			}
        		}

        		break;

          	case 2:

          		
          		pthread_mutex_lock(&tmp->lock);

        		// retrieve del pacchetto da ritrasmettere
        		tmp->shared_tim = tmr->tidp;

        		
	            tmp->thread_retr_fin = 1;
        		
        		//signal per sbloccare il thread di ritrasmissione
	            if((ret = pthread_cond_signal(&tmp->condition)) != 0){
	        		printf("\npthread_cond_signal error -err code: %d-", ret);
	        		exit(-1);
	        	}
	           
        		if((ret = pthread_mutex_unlock(&tmp->lock)) != 0){
        			printf("\n(1)pthread_mutex_unlock error - err code: %d", ret);
        			exit(-1);
        		}
        		
        		//pthread_mutex_lock(&tmp->lock2);
        		
        		while(1){
        			if(tmp->thread_retr_ord == 1){
        				tmp->thread_retr_ord = 0;
        				break;
        			}
        		}

        		break;

            case 3:

            	// operazione di upload terminata.. procedo ad eliminare il thread atto a svolgere tale operazione (thread upload)
            	if(tmp->closing_ack == 1){
            		
					if(pthread_cancel(tmp->upload_tid) != 0){
						perror("\n pthread_cancel error");
						exit(-1);
					}

					tmp->upload_tid = NULL;


					oper_timer.sem_num = 0;
			      	oper_timer.sem_op = -1;
			      	oper_timer.sem_flg = 0;

re_wait_t7:
			      	//wait
			      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
			         	perror("Error waiting t7\n");
			         	if(errno == EINTR){
			         		goto re_wait_t7;
			         	}
			         	exit(-1);
			      	}
						
					
					//print_list_tmr(tmp->timer_list_client);

					// abbiamo invertito per evitare che il valore di tidp fosse eliminato dalla delete_id_timer-
					delete_timer(tmr->tidp, tmr->sockfd, &tmp->timer_list_client);
					delete_id_timer(tmr->tidp, tmr->sockfd, &tmp->timer_id_list_client);
					
					tmp->closing_ack = 0;
			      	tmp->upload_closing = 0;
			      	tmp->flag_last_ack_sent = 0;
			      	tmp->flag_upload_wait = 0;
			      	tmp->var_req = 0;
			      	tmp->ok_phase = 1;
			      	tmp->create_file = 0;

			      	

					oper_timer.sem_num = 0;
			      	oper_timer.sem_op = 1;
			      	oper_timer.sem_flg = 0;

re_signal_t2:

			      	//signal
			      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
			         	perror("Error signal t2\n");
			         	if(errno == EINTR){
			         		goto re_signal_t2;
			         	}
			         	exit(-1);
			      	}
					
			      	
			      	printf("\n ---------> Task upload completed... Bye!");
			      	fflush(stdout);

			      	//mask_sig_unlock();
			      	continue;

  				}

            	pthread_mutex_lock(&tmp->lock);

        		// retrieve del pacchetto da ritrasmettere
        		tmp->shared_tim = tmr->tidp;

        		tmp->thread_retr_fin = 1;
        		
        		//signal per sbloccare il thread di ritrasmissione
	            if((ret = pthread_cond_signal(&tmp->condition)) != 0){
	        		printf("\npthread_cond_signal error -err code: %d-", ret);
	        		exit(-1);
	        	}
	           
        		
        		if((ret = pthread_mutex_unlock(&tmp->lock)) != 0){
        			printf("\n(1)pthread_mutex_unlock error - err code: %d", ret);
        			exit(-1);
        		}
        		
        		//pthread_mutex_lock(&tmp->lock2);
        		
        		while(1){
        			if(tmp->thread_retr_ord == 1){
        				tmp->thread_retr_ord = 0;
        				break;
        			}
        		}

        		break;
        }
		
	}
}


static timer_t srtSchedule(int sockfd, int timerid,char *sequence_number,char *SYNbit,char *ACKbit,char *ack_number,char *message,char *operation_no,char *FINbit, char *port,char *size){
    timer_t rc;
    rc = makeTimer(sockfd, timerid, sequence_number, SYNbit, ACKbit, ack_number, message, operation_no, FINbit, port, size);
    return rc;
}

static timer_t srtScheduleS(int sockfd, int timerid){
    timer_t rc;
    rc = makeTimerS(sockfd, timerid);
    return rc;
}




void print_list(t_client *list_head_ptr){

	 t_client *n;

	 printf("\n\n\n\n\n\n ****************************Lista collegata client: ");
	 for (n=list_head_ptr;n!=NULL;n=n->next){
	 	 printf("(%hu - %s - %d) + ", n->port, n->ipaddr, n->sockfd);
	 }
}

void print_list_tmr(t_tmr *list_head_ptr_tmrs){

	 t_tmr *n;

	 printf("\nLista collegata timers: ");
	 for (n=list_head_ptr_tmrs;n!=NULL;n=n->next){
	 	 printf("\nTIMER: %p", n->idTimer);
	 	 fflush(stdout);
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

void print_list_des(t_d *list_head_ptr){
	 t_d *n;

	 printf("\nLista collegata descrittori: ");
	 for (n=list_head_ptr;n!=NULL;n=n->next){
	 	 printf("(%d) - ", n->ds);
	 }
}


// funzione per eliminare il descrittore della socket del client in fase di eliminazione di un client durante la chiusura da parte del client o da parte del server
void delete_descriptor(int sock_ds, t_d **list_head_ptr_des){

	t_d *previous;
	t_d *tmp;
	t_d *current = *list_head_ptr_des;

	if(current->ds == sock_ds){
	 	//eliminazione in testa
	 	tmp = *list_head_ptr_des;
	 	*list_head_ptr_des = tmp->next;
	 	free(tmp);
	 	close(sock_ds);
	 	return;
	}

	while (current != NULL){
	 	if(current->ds == sock_ds){
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
	 	close(sock_ds);
	 	free(tmp);
	 	return;
	}
}


// funzione per deallocare lo spazio riservato ad un client nel momento della chiusura della connessione da parte del client o del server
void delete(unsigned short num_port, char* ip_address, t_client **list_head_ptr, t_d **list_head_ptr_des){

	 t_client *previous;
	 t_client *tmp;
	 t_client *current = *list_head_ptr;
	
	 if(current->port == num_port && (strcmp(current->ipaddr, ip_address) == 0)){
	 	//eliminazione in testa
	 	tmp = *list_head_ptr;
	 	*list_head_ptr = tmp->next;
	 	list_head = *list_head_ptr;
	 	delete_descriptor(tmp->sockfd, list_head_ptr_des);
	 	tmp->expected_next_seq_number = 0;
	 	if(pthread_cancel(tmp->retrasmission_thread) != 0){
	 		perror("pthread_cancel error");
	 		exit(-1);
	 	}

	 	free(tmp);
	 	count_client--;
	 	return;
	 }

	 while (current != NULL){
 		if(current->port == num_port && (strcmp(current->ipaddr, ip_address) == 0)){
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
	 	 delete_descriptor(tmp->sockfd, list_head_ptr_des);
	 	 free(tmp);
	 	 count_client--;
	 	 return;
	 }
}

// funzione per eliminare lo spazio dedicato ai pacchetti salvati per la ritrasmissione in caso d tre ack duplicati
void delete_sent_packet(char *seq_num, int sockfd, t_sent_pkt **list_head_ptr_sent_pkts){

   t_sent_pkt *previous;
   t_sent_pkt *tmp_sent_pkt = NULL;
   t_sent_pkt *current = *list_head_ptr_sent_pkts;

   t_client *dest_client;	
   for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
	    if(dest_client->sockfd == sockfd){
		    break;
	    }  
   }

   if((atoi(current->sequence_number) == atoi(seq_num)) && (current->sockfd == sockfd)){
    	//eliminazione in testa
    	tmp_sent_pkt = current;
    	*list_head_ptr_sent_pkts = tmp_sent_pkt->next;
    	dest_client->list_sent_pkt = *list_head_ptr_sent_pkts;
    	free(tmp_sent_pkt);
    	return;
   }

   while (current != NULL){
      	if((atoi(current->sequence_number) == atoi(seq_num)) && (current->sockfd == sockfd)){
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

// funzione per eliminare lo spazio di memoria usato per salvare le informazioni di un pacchetto associaro ad un timer
void delete_timer(timer_t idT, int sockfd, t_tmr **list_head_ptr_tmrs){
   

   t_client *dest_client;

   for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
      if(dest_client->sockfd == sockfd){
	     break;
      }  
   }
   
   t_tmr *previous;
   t_tmr *tmp_timer = NULL;
   t_tmr *current = *list_head_ptr_tmrs;

   //print_list_tmr(*list_head_ptr_tmrs);
   
   
   if(current->idTimer == idT){
   		
    	//eliminazione in testa
    	tmp_timer = current;
    	*list_head_ptr_tmrs = tmp_timer->next;
    	dest_client->timer_list_client = *list_head_ptr_tmrs;
    	free(tmp_timer);
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
   
     tmp_timer = current;
     previous->next = current->next;
     free(tmp_timer);
     return;
   }
}


// funzione per deallocare lo spazio di memoria riservato ai pacchetti bufferizzati in caso di arrivo fuori sequenza
void delete_buffered_packet(char *seq_num, int sockfd, t_buffered_pkt **list_head_ptr_buffered_pkts){

   t_client *dest_client;

   for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
        if(dest_client->sockfd == sockfd){
		    break;
        }  
   }	
   
   t_buffered_pkt *previous;
   t_buffered_pkt *tmp;
   t_buffered_pkt *current = *list_head_ptr_buffered_pkts;
   
   if(atoi(current->sequence_number) == atoi(seq_num)){
    //eliminazione in testa
    tmp = *list_head_ptr_buffered_pkts;
    *list_head_ptr_buffered_pkts = tmp->next;
    dest_client->buff_pkt_client = *list_head_ptr_buffered_pkts;
    
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
void delete_id_timer(timer_t tidp, int sockfd, t_timid **list_head_ptr_id_tmrs){
   
   t_client *dest_client;

   for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
        if(dest_client->sockfd == sockfd){
		    break;
        }  
   }     //mi da errore perchè non conosce la socket

   t_timid *previous;
   t_timid *tmp_timer = NULL;
   t_timid *current = *list_head_ptr_id_tmrs;

   //print_list_tmr(*list_head_ptr_id_tmrs);
    
   if(current->tidp == tidp){
    	//eliminazione in testa
    	tmp_timer = current;
    	*list_head_ptr_id_tmrs = tmp_timer->next;
    	dest_client->timer_id_list_client = *list_head_ptr_id_tmrs;
    	free(tmp_timer);
    	tmp_timer = NULL;
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
     tmp_timer = NULL;
     return;
   }
}


// funzione delegata alla gestione del ctrl+C da parte del server
void handler(){

	char buffer[MAXLINE];
	int sig_sock;
	t_client *n;
	timer_t ret_timer;
	struct sembuf oper_timer;
	float loss_p;
	memset(buffer, 0, MAXLINE);

	con_close = 1;
	final_counter = count_client;
	final_counter2 = count_client;

	if(count_client==0){
		printf("Clients all out\n");
		exit(1);
	}

	print_list(list_head);

	//mando pkt con numero casuale ad ogni client
	for (n = list_head; n != NULL; n = n->next){
		t_data *new_data = malloc(sizeof(t_data));
		if(new_data == NULL){
			perror("malloc error");
			exit(1);
		}
		
		n->rand_c = rand()%10000;
		
		//devo confrontare giù con i random di tutti i client,num era il numero fisso scelto prima
		strcpy(new_data->FINbit, "1\0");
		snprintf(new_data->sequence_number, sizeof(new_data->sequence_number), "%d", n->rand_c);
		
	 	cliaddr.sin_port = htons(n->port);
	 	inet_aton(n->ipaddr, &cliaddr.sin_addr);
	 	cliaddr.sin_family = AF_INET;
	 	sig_sock = n->sockfd;
	 	
	    oper_timer.sem_num = 0;
	    oper_timer.sem_op = -1;
	  	oper_timer.sem_flg = 0;

rewait_ctrlc:

	  	//wait sul valore timer
	  	if((semop(n->semid_timer, &oper_timer, 1)) == -1){
	      perror("Error waiting ctrlc\n");
	      if(errno == EINTR){
	        goto rewait_ctrlc;
	      }
	      
	      exit(-1);
	  	}

	 	loss_p = float_rand(0.0, 1.0);
			
		if(loss_p >= LOSS_PROBABILITY){
			if((sendto(sig_sock, new_data, 1500, 0, (struct sockaddr*)&cliaddr, sizeof(cliaddr))) < 0){
				perror("Errore in sendto\n");
				exit(1);
			}
		}


		ret_timer = srtSchedule(n->sockfd, n->timerid, new_data->sequence_number, new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port,new_data->size);
#ifdef ADPTO
		append_timer_adaptive(ret_timer, new_data->sequence_number, &n->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,n->sockfd, new_data->port,new_data->size, n->rto, 0.0, 0.0, 0);
#else
		append_timer(ret_timer, new_data->sequence_number, &n->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,n->sockfd, new_data->port,new_data->size);
#endif
		n->timerid++;
		
	    oper_timer.sem_num = 0;
	    oper_timer.sem_op = 1;
	  	oper_timer.sem_flg = 0;

resignal_ctrlc:
	  	  
	  	//wait sul valore timer
	  	if((semop(n->semid_timer, &oper_timer, 1)) == -1){
	      perror("Error signaling ctrlc\n");
	      if(errno == EINTR){
	        goto resignal_ctrlc;
	      }
	      
	      exit(-1);
	  	}

		free(new_data);
	}
}


// funzione per gestire la ritrasmissione in caso di ricezione di tre ack duplicati
void three_duplicate_message(char *ack_num, int sockfd){

	t_sent_pkt *n;
	t_tmr *del_timer;
	char *port;
	t_client *dest_client;
	struct sockaddr_in cliaddr_dest;
	struct sembuf oper_recv,oper_turn;
	timer_t ret_timer;
	float loss_p;
	int ret;

	for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
        if(dest_client->sockfd == sockfd){
    		cliaddr_dest.sin_port = htons(dest_client->port);
		    inet_aton(dest_client->ipaddr, &cliaddr_dest.sin_addr);
		    cliaddr_dest.sin_family = AF_INET;
		    break;
        }  
    }

    oper_turn.sem_num = 0;
    oper_turn.sem_op = -1;
  	oper_turn.sem_flg = 0;

resignaling1:
  	  
  	//wait sul valore timer
  	if((semop(dest_client->semid_retr_turn, &oper_turn, 1)) == -1){
      perror("Error signaling 1\n");
      if(errno == EINTR){
        goto resignaling1;
      }
      
      exit(-1);
  	}

	for(n = dest_client->list_sent_pkt; n != NULL; n = n->next){
		if(atoi(n->sequence_number) == atoi(ack_num) && n->sockfd == sockfd) {
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

		 	// popola la porta verso il quale mandare il nome del file da scaricare nel caso del download
			if(strcmp(n->operation_no, "2") == 0){
			  	asprintf(&port, "%d", dest_client->port_download);
	  			strcpy(new_data->port_download, port);
			}

			for(del_timer = dest_client->timer_list_client; del_timer != NULL; del_timer = del_timer->next){
				if((atoi(del_timer->sequence_number) == atoi(ack_num)) && (del_timer->sockfd == sockfd)){

					ret_timer = srtSchedule(dest_client->sockfd, dest_client->timerid, new_data->sequence_number,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit, new_data->port,new_data->size);
#ifdef ADPTO
					append_timer_adaptive(ret_timer, new_data->sequence_number, &dest_client->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,dest_client->sockfd, new_data->port,new_data->size, dest_client->rto, del_timer->last_rto, del_timer->sample_value, 1);
#else
					append_timer(ret_timer, new_data->sequence_number, &dest_client->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,dest_client->sockfd, new_data->port,new_data->size);					
#endif
					dest_client->timerid++; 

					ret = timer_delete(del_timer->idTimer);
					if(ret == -1){
						perror("\n(2) Errore in cancellazione ret_timer\n");
						exit(1);
					}
					delete_id_timer(del_timer->idTimer, dest_client->sockfd, &dest_client->timer_id_list_client);
	  				delete_timer(del_timer->idTimer, dest_client->sockfd, &dest_client->timer_list_client);

	  				break;
				}
			}


		 	// sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
			loss_p = float_rand(0.0, 1.0);
			
			if(loss_p >= LOSS_PROBABILITY){
			  	if(sendto(sockfd, new_data, 1500, 0, (struct sockaddr *)&cliaddr_dest, (socklen_t)sizeof(cliaddr_dest)) < 0){
			       perror("\n(1)errore in sendto retransmission");
			       exit(1);
			  	}   
			}
			
							

			oper_recv.sem_num = 0;
		  	oper_recv.sem_op = 1;
		  	oper_recv.sem_flg = 0;

resignal_f2:
		  	// signal
		  	if((semop(dest_client->semid_client, &oper_recv, 1)) == -1){
		   	  perror("Error waiting 2\n");
		   	  if(errno == EINTR){
		   	  	goto resignal_f2;
		   	  }
		      exit(-1);
		  	} 	
	
		    oper_turn.sem_num = 0;
		 	oper_turn.sem_op = 1;
			oper_turn.sem_flg = 0;

resignal_f3:
			//signal sul turno di retransmit
			if((semop(dest_client->semid_retr_turn, &oper_turn, 1)) == -1){
		 	   perror("Error signaling 4\n");
		 	   if(errno == EINTR){
		 	   	goto resignal_f3;
		 	   }
		 	   exit(-1);
			}

		    free(new_data);
		    return;
		}
	}

resignaling_three:

	oper_turn.sem_num = 0;
    oper_turn.sem_op = 1;
  	oper_turn.sem_flg = 0;

  	  
  	//wait sul valore timer
  	if((semop(dest_client->semid_retr_turn, &oper_turn, 1)) == -1){
      perror("Error resignaling three\n");
      if(errno == EINTR){
        goto resignaling_three;
      }
      
      exit(-1);
  	}
}

// funzione per gestire la richiesta di chiusura connessione da parte di uno dei client
void *handler_client_closure(void *client){

	t_client *tmp = (t_client *)client;
	int ret_val;
	float loss_p;
	int n, ret;
	char random[10];
	timer_t ret_timer;
	struct sockaddr_in cliaddr, cliaddr2;
	struct sembuf oper_control;
	socklen_t len2;

	
	cliaddr.sin_port = htons(tmp->port);
	inet_aton(tmp->ipaddr, &cliaddr.sin_addr);
	cliaddr.sin_family = AF_INET;

	t_data *new_data_fin2 = malloc(sizeof(t_data));
    if(new_data_fin2 == NULL){
    	perror("\nmalloc error");
    	exit(1);
    }

    //srand(time(NULL));
	tmp->rand_hs = rand()%10000;
	snprintf(new_data_fin2->sequence_number, sizeof(new_data_fin2->sequence_number), "%d", tmp->rand_hs);
    strcpy(new_data_fin2->FINbit, "1\0");
    strcpy(new_data_fin2->ACKbit, "0\0");
    
    loss_p = float_rand(0.0,1.0);
    if(loss_p >= LOSS_PROBABILITY){
    	if(sendto(tmp->sockfd, new_data_fin2, 1500, 0, (struct sockaddr *)&cliaddr, (socklen_t)sizeof(cliaddr)) < 0){
            perror("(2)errore in sendto");
            exit(1);
        }
    }

    oper_control.sem_num = 0;
	oper_control.sem_op = -1;
	oper_control.sem_flg = 0;

rewait_f4:
	  	  
	//signal su lista timer
	if((semop(tmp->semid_timer, &oper_control, 1)) == -1){
		perror("Error signaling 28\n");
		if(errno == EINTR){
			goto rewait_f4;
		}
		exit(-1);
	}

	//tmp3->flag_last_ack_sent = 1;
    ret_timer = srtSchedule(tmp->sockfd,tmp->timerid,new_data_fin2->sequence_number, new_data_fin2->SYNbit,new_data_fin2->ACKbit,new_data_fin2->ack_number,new_data_fin2->message,new_data_fin2->operation_no,new_data_fin2->FINbit, new_data_fin2->port,new_data_fin2->size);
	
#ifdef ADPTO
	append_timer_adaptive(ret_timer, new_data_fin2->sequence_number, &tmp->timer_list_client, new_data_fin2->SYNbit, new_data_fin2->ACKbit, new_data_fin2->ack_number,new_data_fin2->message,new_data_fin2->operation_no,new_data_fin2->FINbit,tmp->sockfd, new_data_fin2->port,new_data_fin2->size, tmp->rto, 0.0, 0.0, 0);
#else
	append_timer(ret_timer, new_data_fin2->sequence_number, &tmp->timer_list_client, new_data_fin2->SYNbit, new_data_fin2->ACKbit, new_data_fin2->ack_number,new_data_fin2->message,new_data_fin2->operation_no,new_data_fin2->FINbit,tmp->sockfd, new_data_fin2->port,new_data_fin2->size);
#endif

	tmp->timerid++;
	tmp->flag_close = 0;
	tmp->second_flag_close = 1;

	oper_control.sem_num = 0;
	oper_control.sem_op = 1;
	oper_control.sem_flg = 0;

resignal_f5:

	//signal su lista timer
	if((semop(tmp->semid_timer, &oper_control, 1)) == -1){
		perror("Error signaling 28\n");
		if(errno == EINTR){
			goto resignal_f5;
		}
		exit(-1);
	}
    

    free(new_data_fin2);
    
    t_data *new_data = malloc(sizeof(t_data));
    if(new_data == NULL){
    	perror("\n malloc error");
    	exit(-1);
    }

rerecvfrom_fin2:
	len2 = sizeof(cliaddr2);
    n = recvfrom(tmp->sockfd, new_data, 1500, 0, (struct sockaddr*)&cliaddr2, (socklen_t *)&len2);
	if(n < 0){
		perror("\n (1)recvfrom error");
		if(errno == EINTR){
			goto rerecvfrom_fin2;
		}
		exit(-1);
	}

	// delete del client..se
	tmp->flag_close = 1;
	snprintf(random, 10, "%d", tmp->rand_hs+1);
	
	if((strncmp(new_data->ack_number, random, sizeof(new_data->ack_number)) == 0) && (strncmp(new_data->ACKbit, "1", 2) == 0)){
		if((tmp->timer_list_client)->idTimer != NULL){
			ret = timer_delete((tmp->timer_list_client)->idTimer);
			if(ret == -1){
				perror("\n timer_delete error");
			}
			delete_id_timer((tmp->timer_list_client)->idTimer,tmp->sockfd,&tmp->timer_id_list_client);
			delete_timer((tmp->timer_list_client)->idTimer,tmp->sockfd,&tmp->timer_list_client);
		}
		
		delete(ntohs(cliaddr2.sin_port), inet_ntoa(cliaddr2.sin_addr), &list_head, &list_head_d);
		print_list(list_head);
    	print_list_des(list_head_d);
	}

	ret_val = 666;
	pthread_exit((void *)&ret_val);
}

void mask_sig(void){
	sigset_t mask;
	sigfillset(&mask);
	pthread_sigmask(SIG_BLOCK, &mask,NULL);
}

void mask_sig_unlock(void){

	sigset_t mask;
	sigemptyset(&mask);
	sigaddset(&mask, SIGRTMIN);
	sigaddset(&mask, SIGINT);
	
	pthread_sigmask(SIG_UNBLOCK, &mask,NULL);
}


// thread incarito di ritrasmettere i pacchetti in caso di scadenza di un timer
void *retrasmission_thread(void *client){
	t_client *tmp = (t_client *)client;
	mask_sig();
    timer_t ret_timer;
    t_tmr *t;
    t_tmr *retr_pack;
    struct sockaddr_in cliaddr_dest;
    pthread_t tid_fin2;
    int ret_val = 666;
    struct sembuf oper_timer, oper_turn, oper_recv, oper_timer_esx;
    float loss_p;
    char *port;
    int find = 0, ret;
    struct sembuf oper_delete;
    
    while(1){

 		if((ret = pthread_mutex_lock(&tmp->lock)) != 0){
 			printf("\n (3)pthread_mutex_lock error - err code: %d", ret);
 			exit(-1);
 		}
 		
 		while(!tmp->thread_retr_fin){
 			if((ret = pthread_cond_wait(&tmp->condition, &tmp->lock)) != 0){
	 			printf("\n (3) pthread_cond_wait error - err code: %d", ret);
	 			exit(-1);
	 		}	
 		}
 		

 		cliaddr_dest.sin_port = htons(tmp->port);
		inet_aton(tmp->ipaddr, &cliaddr_dest.sin_addr);
		cliaddr_dest.sin_family = AF_INET;

		//wait sul timer 
	    oper_timer_esx.sem_num = 0;
  	    oper_timer_esx.sem_op = -1;
  	    oper_timer_esx.sem_flg = 0;

rewait_f61:
  	  
  	    if((semop(tmp->semid_timer, &oper_timer_esx, 1)) == -1){
     		perror("Error waiting 3\n");
     		if(errno == EINTR){
     			goto rewait_f61;
     		}

     		exit(-1);
  	  	}

        oper_turn.sem_num = 0;
        oper_turn.sem_op = -1;
  	    oper_turn.sem_flg = 0;

rewait_f71:
  	  
  	  //wait sul valore timer
  	    if((semop(tmp->semid_retr_turn, &oper_turn, 1)) == -1){
     	    perror("Error waiting 22\n");
     	  	if(errno == EINTR){
     	  		goto rewait_f71;
     	    }
     	    exit(-1);
  	    }

  	    find = 0;
  	    
  	    for(t = tmp->timer_list_client; t != NULL; t = t->next){
		  	if(t->idTimer == tmp->shared_tim){
		  		retr_pack = t;
		  		if(atoi(t->sequence_number) < tmp->last_ack){
		  			break;
		  		}
		  		find = 1;
		  		break;
		  	}

		}

	    if(find == 0 || tmp->flag_close == 1){

		  	if(tmp->flag_close == 1){
		  	
				delete_id_timer(retr_pack->idTimer, tmp->sockfd, &tmp->timer_id_list_client);
		  		delete_timer(retr_pack->idTimer, tmp->sockfd, &tmp->timer_list_client);

		  		// thread per gestione ritrasmissione
	          	ret = pthread_create(&tid_fin2, NULL, handler_client_closure, ((void *)tmp));
	          	if(ret != 0){
	          		perror("\npthread_create error in retransmission");
	          		exit(-1);
	          	}
		  	}
		  	
		  	oper_turn.sem_num = 0;
		 	oper_turn.sem_op = 1;
			oper_turn.sem_flg = 0;

resignal_f81:

			//signal sul turno di retransmit
			if((semop(tmp->semid_retr_turn, &oper_turn, 1)) == -1){
		 	   perror("Error signaling 6\n");
		 	   if(errno == EINTR){
		 	   	goto resignal_f81;
		 	   }
		 	   exit(-1);
			}

			// pacchetto da ritrasmettere non trovato --> pthread_exit()
		  	oper_delete.sem_num = 0;
		  	oper_delete.sem_op = 1;
		  	oper_delete.sem_flg = 0;

resignal_f91:

		  	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
		       perror("Error waiting 4\n");
		       if(errno == EINTR){
		 	   	goto resignal_f91;
		 	   }
		       exit(-1);
		  	}

		  	//pthread_mutex_unlock(&tmp->lock2);
		  	pthread_mutex_unlock(&tmp->lock);
		  	tmp->thread_retr_fin = 0;
		  	tmp->thread_retr_ord = 1;

			continue;
		}

		// retransmit del pacchetto n
		t_data *new_data = malloc(sizeof(t_data));
		if(new_data == NULL){
		    perror("malloc error");
		    exit(1);
		}

		// popolo new_data->sequence
		strcpy(new_data->sequence_number,retr_pack->sequence_number);

		// popolo il new_data->operation_no
		strcpy(new_data->operation_no, retr_pack->operation_no);

		// popolo il campo new_data->ack_number
		strcpy(new_data->ack_number, retr_pack->ack_number);
		  
		// popolo il new_data->message
		memcpy(new_data->message, retr_pack->message, MAXLINE);
		  
		// popola la porta verso la quale ritrametto
		strcpy(new_data->port, retr_pack->port);

		if(tmp->filename_bytes == 1){
		  	strcpy(new_data->size,retr_pack->size);
		}

		if(tmp->hs == 1){
		  	strcpy(new_data->SYNbit,retr_pack->SYNbit);
		  	strcpy(new_data->ACKbit,retr_pack->ACKbit);
		}

		if(tmp->second_flag_close == 1){
		  	strcpy(new_data->FINbit,retr_pack->FINbit);
		  	strcpy(new_data->ACKbit,retr_pack->ACKbit);
		}

		if(con_close == 1){
		  	strcpy(new_data->FINbit,retr_pack->FINbit);
		}

		// popola la porta verso il quale mandare il nome del file da scaricare nel caso del download
		if(strcmp(retr_pack->operation_no, "2") == 0){
		  	asprintf(&port,"%d",tmp->port_download);
		  	strcpy(new_data->port_download, port);
		}

	    ret_timer = srtSchedule(tmp->sockfd, tmp->timerid, new_data->sequence_number, new_data->SYNbit, new_data->ACKbit, new_data->ack_number,new_data->message, new_data->operation_no,new_data->FINbit,new_data->port,new_data->size);
#ifdef ADPTO
		append_timer_adaptive(ret_timer, new_data->sequence_number, &tmp->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,tmp->sockfd, new_data->port,new_data->size, tmp->rto, retr_pack->last_rto, retr_pack->sample_value, 1); 
#else
		append_timer(ret_timer, new_data->sequence_number, &tmp->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,tmp->sockfd, new_data->port, new_data->size); 	  
#endif	  
		 
		  // sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
		loss_p = float_rand(0.0, 1.0);
		
		if(loss_p >= LOSS_PROBABILITY){
		    if(sendto(tmp->sockfd, new_data, 1500, 0, (struct sockaddr *)&cliaddr_dest, (socklen_t)sizeof(cliaddr_dest)) < 0){
		       perror("\n(1)errore in sendto retransmission");
		       exit(1);
		  	}   
		}

		tmp->timerid++;
		  
		if(tmp->timer_id_list_client != NULL && tmp->timer_list_client != NULL){
			ret = timer_delete(retr_pack->idTimer);
			if(ret == -1){
				perror("\n(3) Errore in cancellazione ret_timer\n");
				exit(1);
			}
		  	delete_id_timer(retr_pack->idTimer, tmp->sockfd, &tmp->timer_id_list_client);
		  	delete_timer(retr_pack->idTimer, tmp->sockfd, &tmp->timer_list_client);
		}
		 
		free(new_data);
     	

     	if(tmp->hs == 0){
	  	    			

		    oper_recv.sem_num = 0;
	  	    oper_recv.sem_op = 1;
	  	    oper_recv.sem_flg = 0;

resignal_f111:

	  	  // signal
	  	    if((semop(tmp->semid_client, &oper_recv, 1)) == -1){
	     	    perror("Error waiting 6\n");
	     	  	if(errno == EINTR){
		 	   		goto resignal_f111;
		 	    }
	     	    exit(-1);
	  	    } 
	    }

	  

  	    oper_timer.sem_num = 0;
        oper_timer.sem_op = 1;
        oper_timer.sem_flg = 0;

resignal_f121:
        //signal sul valore timer
        if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error signaling 7\n");
         	if(errno == EINTR){
		 	    goto resignal_f121;
		 	}
         	exit(-1);
        }	

	    oper_turn.sem_num = 0;
        oper_turn.sem_op = 1;
  	    oper_turn.sem_flg = 0;

resignal_f131:

  	    //signal sul valore timer
  	    if((semop(tmp->semid_retr_turn, &oper_turn, 1)) == -1){
     	    perror("Error signaling 8\n");
     	    if(errno == EINTR){
	 	   		goto resignal_f131;
	 	  	}
     	    exit(-1);
  	    }
	 
  	    //pthread_mutex_unlock(&tmp->lock2);
  	    
  	    pthread_mutex_unlock(&tmp->lock);
  	    tmp->thread_retr_fin = 0;
  	    tmp->thread_retr_ord = 1;
	    
	}

}

// thread usata per la ritrasmissione in caso di handshake e chiusura connessione
void *handling_timeout_retransmission(void *n){

	  mask_sig();
	  timer_t ret_timer;
	  t_timid *strid = (t_timid *)n;
	  //t_tmr *retr_pack = (t_tmr *)n;
	  t_tmr *retr_pack;
	  int find_sock = 0;
	  struct sockaddr_in cliaddr_dest;
	  t_client *dest_client;
	  pthread_t tid_fin2;
	  int ret_val = 666;
	  struct sembuf oper_timer, oper_turn, oper_recv, oper_timer_esx;
	  float loss_p;
	  t_tmr *t;
	  char *port;
	  int find = 0, ret, sem_val;
	  struct sembuf oper_delete;

	  if(strid == NULL){
	  	//nodo id timer eliminato nel frattempo
	  	pthread_exit((void *)&ret_val);
	  }

	  for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
        if(dest_client->sockfd == strid->sockfd){
        	find_sock = 1;
        	dest_client->retr_phase = 1;
    		cliaddr_dest.sin_port = htons(dest_client->port);
		    inet_aton(dest_client->ipaddr, &cliaddr_dest.sin_addr);
		    cliaddr_dest.sin_family = AF_INET;
		    //printf("\n(RETR_THREAD)--> CLIENT: IP: %s - PORT: %d", dest_client->ipaddr, dest_client->port);

		    sem_val = dest_client->semid_timer;

		    break;
        }  
      }


      if(find_sock == 0){
      	//socket non trovata--->è stata eliminata nel frattempo
      	pthread_exit((void *)&ret_val);
      }

      
      //wait sul timer 
	  oper_timer_esx.sem_num = 0;
  	  oper_timer_esx.sem_op = -1;
  	  oper_timer_esx.sem_flg = 0;

rewait_f6:
  	  
  	  if((semop(sem_val, &oper_timer_esx, 1)) == -1){
     	perror("Error waiting 3\n");
     	if(errno == EINTR){
     		goto rewait_f6;
     	}

     	exit(-1);
  	  }

      oper_turn.sem_num = 0;
      oper_turn.sem_op = -1;
  	  oper_turn.sem_flg = 0;

rewait_f7:
  	  
  	  //wait sul valore timer
  	  if((semop(dest_client->semid_retr_turn, &oper_turn, 1)) == -1){
     	  perror("Error waiting 22\n");
     	  if(errno == EINTR){
     	  	goto rewait_f7;
     	  }
     	  exit(-1);
  	  }

	  //controllo se il pkt è stato eliminato da un ack cumulativo
	  for(t = dest_client->timer_list_client; t != NULL; t = t->next){
	  	if(t->idTimer == strid->tidp){
	  		retr_pack = t;
	  		if(atoi(t->sequence_number) < dest_client->last_ack){
	  			break;
	  		}
	  		find = 1;
	  		break;
	  	}

	  }

	  if(find == 0 || dest_client->flag_close == 1){

	  	if(dest_client->flag_close == 1){
	  	
			delete_id_timer(retr_pack->idTimer, dest_client->sockfd, &dest_client->timer_id_list_client);
	  		delete_timer(retr_pack->idTimer, dest_client->sockfd, &dest_client->timer_list_client);

	  		// thread per gestione ritrasmissione
          	ret = pthread_create(&tid_fin2, NULL, handler_client_closure, ((void *)dest_client));
          	if(ret != 0){
          		perror("\npthread_create error in retransmission");
          		exit(-1);
          	}
	  	}
	  	
	  	oper_turn.sem_num = 0;
	 	oper_turn.sem_op = 1;
		oper_turn.sem_flg = 0;

resignal_f8:

		//signal sul turno di retransmit
		if((semop(dest_client->semid_retr_turn, &oper_turn, 1)) == -1){
	 	   perror("Error signaling 6\n");
	 	   if(errno == EINTR){
	 	   	goto resignal_f8;
	 	   }
	 	   exit(-1);
		}

		// pacchetto da ritrasmettere non trovato --> pthread_exit()
	  	oper_delete.sem_num = 0;
	  	oper_delete.sem_op = 1;
	  	oper_delete.sem_flg = 0;

resignal_f9:

	  	if((semop(dest_client->semid_timer, &oper_delete, 1)) == -1){
	       perror("Error waiting 4\n");
	       if(errno == EINTR){
	 	   	goto resignal_f9;
	 	   }
	       exit(-1);
	  	}

		pthread_exit((void *)&ret_val);
		
	  }


	  
	  // retransmit del pacchetto n
	  t_data *new_data = malloc(sizeof(t_data));
	  if(new_data == NULL){
	      perror("malloc error");
	      exit(1);
	  }

	  // popolo new_data->sequence
	  strcpy(new_data->sequence_number,retr_pack->sequence_number);

	  // popolo il new_data->operation_no
	  strcpy(new_data->operation_no, retr_pack->operation_no);

	  // popolo il campo new_data->ack_number
	  strcpy(new_data->ack_number, retr_pack->ack_number);
	  
	  // popolo il new_data->message
	  memcpy(new_data->message, retr_pack->message, MAXLINE);
	  
	  // popola la porta verso la quale ritrametto
	  strcpy(new_data->port, retr_pack->port);

	  if(dest_client->filename_bytes == 1){
	  	strcpy(new_data->size,retr_pack->size);
	  }

	  if(dest_client->hs == 1){
	  	strcpy(new_data->SYNbit,retr_pack->SYNbit);
	  	strcpy(new_data->ACKbit,retr_pack->ACKbit);
	  }

	  if(dest_client->second_flag_close == 1){
	  	strcpy(new_data->FINbit,retr_pack->FINbit);
	  	strcpy(new_data->ACKbit,retr_pack->ACKbit);
	  }

	  if(con_close == 1){
	  	strcpy(new_data->FINbit,retr_pack->FINbit);
	  }

	  // popola la porta verso il quale mandare il nome del file da scaricare nel caso del download
	  if(strcmp(retr_pack->operation_no, "2") == 0){
	  	asprintf(&port,"%d",dest_client->port_download);
	  	strcpy(new_data->port_download, port);
	  }

	  ret_timer = srtSchedule(dest_client->sockfd, dest_client->timerid,new_data->sequence_number, new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,new_data->port,new_data->size);
#ifdef ADPTO
	  append_timer_adaptive(ret_timer, new_data->sequence_number, &dest_client->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,dest_client->sockfd, new_data->port,new_data->size, dest_client->rto, retr_pack->last_rto, retr_pack->sample_value, 1); 
#else
	  append_timer(ret_timer, new_data->sequence_number, &dest_client->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,dest_client->sockfd, new_data->port, new_data->size); 	  
#endif	  
	 

	  // sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
	  loss_p = float_rand(0.0, 1.0);
	
	  if(loss_p >= LOSS_PROBABILITY){
	  	 if(sendto(dest_client->sockfd, new_data, 1500, 0, (struct sockaddr *)&cliaddr_dest, (socklen_t)sizeof(cliaddr_dest)) < 0){
	       perror("\n(1)errore in sendto retransmission");
	       exit(1);
	  	 }   
	  }

	  dest_client->timerid++;
	  
	  if(dest_client->timer_id_list_client != NULL && dest_client->timer_list_client != NULL){
	  	delete_id_timer(retr_pack->idTimer, dest_client->sockfd, &dest_client->timer_id_list_client);
	  	delete_timer(retr_pack->idTimer, dest_client->sockfd, &dest_client->timer_list_client);
	  }
	 
	  free(new_data);

	  if(dest_client->hs == 0){
	  	  
		  oper_recv.sem_num = 0;
	  	  oper_recv.sem_op = 1;
	  	  oper_recv.sem_flg = 0;

resignal_f11:

	  	  // signal
	  	  if((semop(dest_client->semid_client, &oper_recv, 1)) == -1){
	     	  perror("Error waiting 6\n");
	     	  if(errno == EINTR){
		 	   	goto resignal_f11;
		 	   }
	     	  exit(-1);
	  	  } 
	  }

	  

  	  oper_timer.sem_num = 0;
      oper_timer.sem_op = 1;
      oper_timer.sem_flg = 0;

resignal_f12:
      //signal sul valore timer
      if((semop(dest_client->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error signaling 7\n");
         	if(errno == EINTR){
		 	  goto resignal_f12;
		 	}
         	exit(-1);
      }	

	  oper_turn.sem_num = 0;
      oper_turn.sem_op = 1;
  	  oper_turn.sem_flg = 0;

resignal_f13:

  	  //signal sul valore timer
  	  if((semop(dest_client->semid_retr_turn, &oper_turn, 1)) == -1){
     	  perror("Error signaling 8\n");
     	  if(errno == EINTR){
	 	   	goto resignal_f13;
	 	  }
     	  exit(-1);
  	  }

  	 
	  //pthread_exit((void *)&ret_val);
	  if(pthread_cancel(pthread_self()) != 0){
		perror("\n pthread_cancel error");
		exit(-1);
	  }

	  return NULL;
	 
     
}

void handling_master_timer(int sockfd, timer_t tidp, t_tmr** list_head_global_tmr){
 	
 	t_tmr* n;
 	t_client* dest_client;
 	timer_t ret_timer;
 	int ret;
 	float loss_p;
 	struct sockaddr_in cliaddr_dest;
 	struct sembuf oper_delete;

 	for(dest_client = list_head; dest_client != NULL; dest_client = dest_client->next){
        if(dest_client->sockfd == sockfd){
        	cliaddr_dest.sin_port = htons(dest_client->port);
		    inet_aton(dest_client->ipaddr, &cliaddr_dest.sin_addr);
		    cliaddr_dest.sin_family = AF_INET;
		    dest_client->master_exists_flag = 0;
		    break;
        }  
    }

    //wait sul timer 
	oper_delete.sem_num = 0;
  	oper_delete.sem_op = -1;
  	oper_delete.sem_flg = 0;

rewait_f14:

  	if((semop(dest_client->semid_timer, &oper_delete, 1)) == -1){
     	perror("Error waiting 3\n");
     	if(errno == EINTR){
	 	   goto rewait_f14;
	 	}
     	exit(-1);
  	}

    for(n = dest_client->timer_list_client; n != NULL; n = n->next){
    	//individuo i pacchetti che devo ritramettere verso il client dest_client
    	if(n->sockfd == sockfd){

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
				perror("\n(3) Errore in cancellazione ret_timer\n");
				exit(1);
			}

			if(&dest_client->timer_id_list_client != NULL && &dest_client->timer_list_client != NULL){
				delete_id_timer(n->idTimer, dest_client->sockfd, &dest_client->timer_id_list_client);
				delete_timer(n->idTimer, dest_client->sockfd, &dest_client->timer_list_client);
			}
			
			ret_timer = srtSchedule(dest_client->sockfd, dest_client->timerid, new_data->sequence_number,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit, new_data->port,new_data->size);
#ifdef ADPTO
			append_timer_adaptive(ret_timer, new_data->sequence_number, &dest_client->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,dest_client->sockfd, new_data->port,new_data->size, dest_client->rto, 0.0, 0.0, 0);
#else
			append_timer(ret_timer, new_data->sequence_number, &dest_client->timer_list_client,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,dest_client->sockfd, new_data->port,new_data->size);			
#endif

			// sto ritrasmettendo un messaggio di richiesta -- > uso SERVADDR per inviare il pacchetto al main thread del server
			loss_p = float_rand(0.0, 1.0);
			if(loss_p >= LOSS_PROBABILITY){
			  	if(sendto(dest_client->sockfd, new_data, 1500, 0, (struct sockaddr *)&cliaddr_dest, (socklen_t)sizeof(cliaddr_dest)) < 0){
			       perror("\n(1)errore in sendto retransmission");
			       exit(1);
			  	}   
			}
			
			dest_client->timerid++;
			
			free(new_data);

    	}
    }

    ret = timer_delete(tidp);
	if(ret == -1){
		perror("\n(1) Errore in cancellazione ret_timer\n");
		//exit(1);
	}

    //signal sul timer 
	oper_delete.sem_num = 0;
  	oper_delete.sem_op = 1;
  	oper_delete.sem_flg = 0;

resignal_f15:

  	if((semop(dest_client->semid_timer, &oper_delete, 1)) == -1){
     	perror("Error waiting 3\n");
     	if(errno == EINTR){
	 	   goto resignal_f15;
	 	}
     	exit(-1);
  	}
     	

 }

// thread incaricato di gestire un eventuale richiesta di upload del client
void *upload(void *s_sock){

	t_client 	*tmp;
	int 		n;
	struct 		sockaddr_in cliaddr2;
	struct 		sockaddr_in cliaddr3;
	struct 		sockaddr_in sin;
	struct 		sembuf  oper_timer;
	int 		send_sock = *((int *)&s_sock);
	int 		fd_up;
	int 		len2;
	FILE 		*file_up = NULL;
	int 		start_write = 0;
	int 		act_seq_num;
	char 		*tmp_seq;
	int  		tmp_pkt;
	char 		*tmp_acknum;
	int 		expected_next_seq_num;
	char 		*new_port;
	int 		port_number;
	int 		len3;
	float       loss_p;
	timer_t 	ret_timer;
	int 		ret;
	char 		path_file[MAXLINE];

	/////////////////////////////////////////////
	
	time_t begin; 
	
	

	
	printf("Starting upload operation...\n\n\n");
	fflush(stdout);

	for (tmp=list_head; tmp!=NULL; tmp=tmp->next){
	 	if(tmp->sockfd == send_sock){
	 	    // verifico che la porta del client sia corretta
		    // send_port = ntohs(cliaddr.sin_port);
			cliaddr2.sin_port = htons(tmp->port);
			inet_aton(tmp->ipaddr, &cliaddr2.sin_addr);
			cliaddr2.sin_family = AF_INET;
			tmp_pkt = tmp->sequence_number;
			if(tmp->expected_next_seq_number != 0){
				expected_next_seq_num = tmp->expected_next_seq_number;
			}

			tmp->upload_tid = pthread_self();
			tmp->operation_value = 3;
			
			break;

	 	}	
	}
	
	while(1){

		// entro qui dopo aver mandato ok al client per inizio dell'operzione di upload
		if(start_write == 1){

			len2 = sizeof(cliaddr2);

rerecvfrom_final_upload:
			
			printf(" ");

			t_data *new_data = malloc(sizeof(t_data));
			if(new_data == NULL){
				perror("malloc error");
				exit(1);
			}	

			n = recvfrom(send_sock, new_data, 1500, 0, (struct sockaddr*)&cliaddr2, (socklen_t *)&len2);
			if(n < 0){
				if(errno == EINTR){
					goto rerecvfrom_final_upload;
				}
				exit(-1);
			}

			if(n == 0){
				if(close(udpfd) == -1){
					perror("errore in close");
					exit(1);
				}
			}
			
			// controllo per ricezione in ordine --> se passa fa quello sotto
			cliaddr3.sin_port = htons(atoi(new_data->port));
			inet_aton(tmp->ipaddr, &cliaddr3.sin_addr);
			cliaddr3.sin_family = AF_INET;

			if(tmp->expected_next_seq_number == atoi(new_data->sequence_number)){	

				if(tmp->create_file == 1){
					
					printf("\n\n\n\n\nSto creando il file %s [%ld bytes]\n\n", new_data->message, strtol(new_data->size, NULL, 10));

					ret = timer_delete((tmp->timer_list_client)->idTimer);    
  					if(ret == -1){
  						if(errno == EINVAL){
  							perror("\neinval error");
  							exit(-1);
  						}

  						if(errno == EINTR){
   	 						//closing connection
  						}

    					perror("\ntimer_delete error");
    					exit(-1);
  					}

  					tmp->file_size = strtol(new_data->size, NULL, 10);

  					delete_id_timer((tmp->timer_list_client)->idTimer, tmp->sockfd, &tmp->timer_id_list_client);
  					delete_timer((tmp->timer_list_client)->idTimer, tmp->sockfd, &tmp->timer_list_client);
					// ho ricevuto il pacchetto contente il nome del file da creare presso la repository del server
					// mando l'ack di corretta ricezione del nome del file

					tmp->flag_last_ack_sent = 1;
					
					t_data *new_data_name = malloc(sizeof(t_data));
					if(new_data_name == NULL){
						perror("\n malloc error");
						exit(-1);
					}

					// popolo sequence_number del packet di risposta
					asprintf(&tmp_seq,"%d",tmp_pkt);
					strcpy(new_data_name->sequence_number,tmp_seq);

					// popolo l'operation_no
					strcpy(new_data_name->operation_no,"3\0");

					// popolo ack_number del packet di risposta
					// popola act_seq_num per sapere il valore del ack_number per la risposta
					act_seq_num = atoi(new_data->sequence_number) + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);
					
					asprintf(&tmp_acknum, "%d", act_seq_num);
					strcpy(new_data_name->ack_number, tmp_acknum);

					//file non presente nel server --> mando stringa di default al client
					strcpy(new_data_name->message, "");

					tmp_pkt = tmp_pkt + DEF_LENGHT;
					tmp->sequence_number = tmp_pkt;
					tmp->ack_number = act_seq_num;

					loss_p = float_rand(0.0, 1.0);
	                if(loss_p >= LOSS_PROBABILITY){
	                	if((sendto(send_sock, new_data_name, 1500, 0,(struct sockaddr*)&cliaddr3, sizeof(cliaddr3))) < 0){
							perror("Errore sendto\n");
							exit(1);
						}
	                }
					
					//expected_next_seq_num = act_seq_num;
					tmp->expected_next_seq_number = act_seq_num;

					free(tmp_seq);
					free(tmp_acknum);
					free(new_data_name);

					strcpy(path_file, "FILES_SERVER/");
                    strcat(path_file, new_data->message);

					fd_up = open(path_file, O_CREAT|O_TRUNC|O_RDWR, 0666);
					if(fd_up == -1){
						perror("open error");
						exit(1);
					}

					file_up = fdopen(fd_up, "w+");
					if(file_up == NULL){
						perror("fdopen error");
						exit(1);
					}

					tmp->create_file = 0;
					free(new_data);

					
				}else{

					if(strcmp(new_data->message, "666") != 0){

						// ho ricevuto una parte del contenuto del file da uploadare nel server
						// mando indietro un ack per il singolo contenuto ricevuto
						if(tmp->file_size >= sizeof(new_data->message)){
							if(fwrite(new_data->message, (size_t)sizeof(new_data->message), 1, file_up) < 0){
								perror("\nfwite error (1)");
								exit(-1);
							}

							fflush(file_up);

							tmp->file_size -= (long int)sizeof(new_data->message);
							
						}
						else{

							if(fwrite(new_data->message, (size_t)tmp->file_size, 1, file_up) < 0){
								perror("\nfwrite error (2)");
								exit(-1);
							}

							fflush(file_up);
							
							tmp->file_size -= (long int)tmp->file_size;
						}
						

						tmp->ack_number = atoi(new_data->sequence_number) + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);
refor:
						// implementare debuffering
						// da verificare se la debufferizzazione avviene in ordine
						// tmp->receive_window++;

			            for(t_buffered_pkt *n = tmp->buff_pkt_client; n != NULL; n = n->next){
			                if(tmp->ack_number == atoi(n->sequence_number)){
			                  
			                    // utilizzo del pacchetto
			                    tmp->ack_number = atoi(n->sequence_number) + (int)strlen(n->message) + (int)strlen(n->operation_no);
			                    
			                      //utilizzo
		                        if(strcmp(n->message,"666") != 0){
		                        	if(tmp->file_size >= sizeof(n->message)){

		                        		if(fwrite(n->message, (size_t)sizeof(n->message), 1, file_up) < 0){
			                                perror("Errore in fwrite (3)\n");
			                                fflush(stdout);
			                                exit(1);
			                            }


			                            fflush(file_up);
										tmp->file_size -= (long int)sizeof(n->message);
			                        
		                        	}
		                        	else{
		                        		if(fwrite(n->message, (size_t)tmp->file_size, 1, file_up) < 0){
			                                perror("Errore in fwrite (4)\n");
			                                fflush(stdout);
			                                exit(1);
			                            }


			                            fflush(file_up);
										tmp->file_size -= (long int)tmp->file_size;
			                        	
		                        	}
		                        }
		                        else{
		                        	tmp->upload_closing = 1;
		                        }

		                        
			                    tmp_pkt += DEF_LENGHT;
			                    tmp->num_pkt_buff--;

			                    // potrebbe non avere senso -- visto che facciamo if sull'uguale 
			                    if(tmp->ack_number < atoi(n->sequence_number) + (int)strlen(n->message) + (int)strlen(n->operation_no)){
			                        tmp->ack_number = atoi(n->sequence_number) + (int)strlen(n->message) + (int)strlen(n->operation_no);
			                    }

			                    delete_buffered_packet(n->sequence_number, tmp->sockfd, &tmp->buff_pkt_client);
								tmp->receive_window++;
			                    goto refor;  // faccio ricominciare il ciclo nel caso in cui i pacchetti siano in ordine dopo l'aggiornamento appena fatto 
			                }
			            }

			            
						t_data *new_data_cont = malloc(sizeof(t_data));
						if(new_data_cont == NULL){
							perror("\n malloc error");
							exit(-1);
						}

						// popolo sequence_number del packet di risposta
						asprintf(&tmp_seq,"%d", tmp_pkt);
						strcpy(new_data_cont->sequence_number, tmp_seq);

						// popolo l'operation_no
						strcpy(new_data_cont->operation_no,"3\0");

						// popolo ack_number del packet di risposta
						// popola act_seq_num per sapere il valore del ack_number per la risposta
						asprintf(&tmp_acknum, "%d", tmp->ack_number);
						strcpy(new_data_cont->ack_number, tmp_acknum);

						//file non presente nel server --> mando stringa di default al client
						strcpy(new_data_cont->message, "");

						tmp_pkt = tmp_pkt + DEF_LENGHT;
						tmp->sequence_number = tmp_pkt;
						tmp->last_ack = tmp->sequence_number;
						
						loss_p = float_rand(0.0, 1.0);
		                
		                if(loss_p >= LOSS_PROBABILITY){
		                	if((sendto(send_sock, new_data_cont, 1500, 0,(struct sockaddr*)&cliaddr3, sizeof(cliaddr3))) < 0){
								perror("Errore sendto\n");
								exit(1);
							}
		                }
						

						//expected_next_seq_num = act_seq_num;
						tmp->expected_next_seq_number = tmp->ack_number;
						
						if(tmp->upload_closing == 1){
							
		
							oper_timer.sem_num = 0;
					      	oper_timer.sem_op = -1;
					      	oper_timer.sem_flg = 0;

re_wait_t:
					      	//wait
					      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
					         	perror("Error waiting t\n");
					         	if(errno == EINTR){
					         		goto re_wait_t;
					         	}
					         	exit(-1);
					      	}


							tmp->flag_last_ack_sent = 1;
							ret_timer = srtSchedule(tmp->sockfd, tmp->timerid, new_data_cont->sequence_number,new_data_cont->SYNbit,new_data_cont->ACKbit,new_data_cont->ack_number,new_data_cont->message,new_data_cont->operation_no,new_data_cont->FINbit, new_data_cont->port,new_data_cont->size);
#ifdef ADPTO
							append_timer_adaptive(ret_timer, new_data_cont->sequence_number, &tmp->timer_list_client, new_data_cont->SYNbit, new_data_cont->ACKbit, new_data_cont->ack_number,new_data_cont->message,new_data_cont->operation_no,new_data_cont->FINbit,tmp->sockfd, new_data_cont->port,new_data_cont->size, tmp->rto, 0.0, 0.0, 0);
#else
							append_timer(ret_timer, new_data_cont->sequence_number, &tmp->timer_list_client, new_data_cont->SYNbit, new_data_cont->ACKbit, new_data_cont->ack_number,new_data_cont->message,new_data_cont->operation_no,new_data_cont->FINbit,tmp->sockfd, new_data_cont->port,new_data_cont->size);							
#endif
							tmp->timerid++;
							
							free(tmp_seq);
							free(tmp_acknum);
							free(new_data_cont);
							free(new_data);
							tmp->closing_ack = 1;

							/////////////////////////////////////////////////////////////////////////////
							time_t end = time(NULL);

							printf("\n TIME: %ju sec", (uintmax_t)(end - begin));
							fflush(stdout);
							/////////////////////////////////////////////////////////////////////////////

							oper_timer.sem_num = 0;
					      	oper_timer.sem_op = 1;
					      	oper_timer.sem_flg = 0;

re_signal_t3:

					      	//wait
					      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
					         	perror("Error signal t3\n");
					         	if(errno == EINTR){
					         		goto re_signal_t3;
					         	}
					         	exit(-1);
					      	}

							goto rerecvfrom_final_upload;
						}

						
						free(tmp_seq);
						free(tmp_acknum);
						free(new_data_cont);
						free(new_data);

					}else{

						// ho ricevuto il pacchetto contente il messaggio '666' (fine contenuto file da uploadare)
						// mando l'ack per questo messaggio e fine.

						t_data *new_data_fin = malloc(sizeof(t_data));
						if(new_data_fin == NULL){
							perror("\n malloc error");
							exit(-1);
						}

						// popolo sequence_number del packet di risposta
						asprintf(&tmp_seq,"%d",tmp_pkt);
						strcpy(new_data_fin->sequence_number,tmp_seq);

						// popolo l'operation_no
						strcpy(new_data_fin->operation_no,"3\0");

						// popolo ack_number del packet di risposta
						// popola act_seq_num per sapere il valore del ack_number per la risposta
						act_seq_num = atoi(new_data->sequence_number) + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);
						asprintf(&tmp_acknum, "%d", act_seq_num);
						strcpy(new_data_fin->ack_number, tmp_acknum);

						//file non presente nel server --> mando stringa di default al client
						strcpy(new_data_fin->message, "");

						tmp_pkt = tmp_pkt + DEF_LENGHT;
						tmp->sequence_number = tmp_pkt;
						tmp->ack_number = act_seq_num;

						loss_p = float_rand(0.0, 1.0);
		                
		                if(loss_p >= LOSS_PROBABILITY){
		                	if((sendto(send_sock, new_data_fin, 1500, 0,(struct sockaddr*)&cliaddr3, sizeof(cliaddr3))) < 0){
								perror("Errore sendto\n");
								exit(1);
							}
		                }
						

						//expected_next_seq_num = act_seq_num;
						tmp->expected_next_seq_number = act_seq_num;
						tmp->last_ack = tmp->sequence_number;
						
						if(file_up != NULL){
							if(fclose(file_up) != 0){
								perror("fclose error");
								exit(-1);
							}
							
							file_up = NULL;
						}
						
						close(fd_up);
						strcpy(buffer, "");
						start_write = 0;
						
						tmp->var_req = 0;
						
						oper_timer.sem_num = 0;
				      	oper_timer.sem_op = -1;
				      	oper_timer.sem_flg = 0;

re_wait_t4:

				      	//wait
				      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
				         	perror("Error waiting t4\n");
				         	if(errno == EINTR){
				         		goto re_wait_t4;
				         	}
				         	exit(-1);
				      	}

						tmp->flag_last_ack_sent = 1;
						ret_timer = srtSchedule(tmp->sockfd, tmp->timerid,new_data_fin->sequence_number,new_data_fin->SYNbit,new_data_fin->ACKbit,new_data_fin->ack_number,new_data_fin->message,new_data_fin->operation_no,new_data_fin->FINbit, new_data_fin->port,new_data_fin->size);
#ifdef ADPTO
						append_timer_adaptive(ret_timer, new_data_fin->sequence_number, &tmp->timer_list_client, new_data_fin->SYNbit, new_data_fin->ACKbit, new_data_fin->ack_number,new_data_fin->message,new_data_fin->operation_no,new_data_fin->FINbit,tmp->sockfd, new_data_fin->port,new_data_fin->size, tmp->rto, 0.0, 0.0, 0);
#else
						append_timer(ret_timer, new_data_fin->sequence_number, &tmp->timer_list_client, new_data_fin->SYNbit, new_data_fin->ACKbit, new_data_fin->ack_number,new_data_fin->message,new_data_fin->operation_no,new_data_fin->FINbit,tmp->sockfd, new_data_fin->port,new_data_fin->size);						
#endif
						tmp->timerid++;
						
						oper_timer.sem_num = 0;
				      	oper_timer.sem_op = 1;
				      	oper_timer.sem_flg = 0;

re_signal_t5:

				      	//wait
				      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
				         	perror("Error singal t5\n");
				         	if(errno == EINTR){
				         		goto re_signal_t5;
				         	}
				         	exit(-1);
				      	}

						tmp->closing_ack = 1;
						free(tmp_seq);
						free(tmp_acknum);
						free(new_data);
						free(new_data_fin);
						// possibile zona in cui l'interrupt per la closing ack

						/////////////////////////////////////////////////////////////////////////////
						time_t end = time(NULL);

						printf("\n TIME: %ju sec", (uintmax_t)(end - begin));
						fflush(stdout);
						/////////////////////////////////////////////////////////////////////////////

						printf("\n\n\nHo finito l'upload del file \n\n\n");

						goto rerecvfrom_final_upload;

					}
				}
			}
			else{
				// gestione della ricezione dei pacchetti fuori ordine --> buffering
				if(atoi(new_data->sequence_number) < tmp->expected_next_seq_number){
					
					t_data *new_data6 = malloc(sizeof(t_data));
                  	if(new_data6 == NULL){
                    	perror("\nmalloc error");
                    	exit(-1);
                  	}

                  	// eventualmente da aggiornare nel momento in cui rivedremo come aggiornare il tmp_pkt con +1 o DEF_LENGHT
                  	
                  	if(asprintf(&tmp_seq,"%d", tmp_pkt) == -1){
                  		perror("\nasprintf error");
                  		exit(-1);
                  	} 

                  	strcpy(new_data6->sequence_number,tmp_seq);
					act_seq_num = tmp->expected_next_seq_number;

                  	// popolo new->ack_number
                  	if(asprintf(&tmp_acknum,"%d",act_seq_num) == -1){
                  		perror("\n asprintf 2 error");
                  		exit(-1);
                  	}
                  	strcpy(new_data6->ack_number, tmp_acknum);
             
                  	// popolo il new_dat->operation_no
                  	strcpy(new_data6->operation_no,"3\0");

                  	// popolo il new_data->message
                  	strcpy(new_data6->message, "");
                 
       
                  	loss_p = float_rand(0.0, 1.0);
                  	
                  	if(loss_p >= LOSS_PROBABILITY){
                    	if(sendto(send_sock, new_data6, 1500, 0, (struct sockaddr *)&cliaddr3, (socklen_t)sizeof(cliaddr3)) < 0){
                      		perror("errore in sendto 4");
                      		exit(1);
                   		}
                  	}
                  	
                  	memset(new_data6->sequence_number, 0, sizeof(new_data6->sequence_number));
                  	memset(new_data6->operation_no, 0, sizeof(new_data6->operation_no));
                  	memset(new_data6->ack_number, 0, sizeof(new_data6->ack_number));
                 	memset(new_data6->message, 0, sizeof(new_data6->message));
                 	
                  	free(tmp_seq);
                  	free(tmp_acknum);
                  	free(new_data);
                  	free(new_data6);

					
				}
				else{

					//expected_next_seq_num > atoi(packet->sequence_number)
			        // bufferizzare.. 
			        if(++tmp->num_pkt_buff <= ((tmp->receive_window)-1)){
			            //packets_buffer = reallocarray(packets_buffer, num_pkt_buff, 1500);
			            append_buffered_packet(new_data->sequence_number, new_data->ack_number, new_data->message, new_data->operation_no, tmp->sockfd, &tmp->buff_pkt_client);
			            tmp->receive_window--;
			        }else{
			            tmp->num_pkt_buff--;
			        }

		          	//nel caso mi arrivi un pkt fuori sequenza ---> io mando a prescindere ack duplicato
		          	t_data *new_data7 = malloc(sizeof(t_data));
		          	if(new_data7 == NULL){
		              	perror("\nmalloc error");
		              	exit(-1);
		          	}

		          	// popolo new_data->sequence
		          	asprintf(&tmp_seq,"%d", tmp_pkt);
		          	strcpy(new_data7->sequence_number,tmp_seq);

		          	// popolo new->ack_number
		          	act_seq_num = tmp->expected_next_seq_number;

		          	asprintf(&tmp_acknum, "%d", act_seq_num);
		          	strcpy(new_data7->ack_number, tmp_acknum);
		      
		          	// popolo il new_dat->operation_no
		          	strcpy(new_data7->operation_no,"3\0");

		          	// popolo il new_data->message
		          	strcpy(new_data7->message, "");
		          	//tmp_pkt = tmp_pkt + DEF_LENGHT;
		          	tmp->sequence_number = tmp_pkt;
					
		          	loss_p = float_rand(0.0, 1.0);
		          	
		          	if(loss_p >= LOSS_PROBABILITY){
		            	if(sendto(send_sock, new_data7, 1500, 0, (struct sockaddr *)&cliaddr3, (socklen_t)sizeof(cliaddr3)) < 0){
		              		perror("errore in sendto 4");
		              		exit(1);
		            	}
		          	}
		          
		          	memset(tmp_acknum, 0, strlen(tmp_acknum));
		          	
		          	free(tmp_seq);
		          	free(tmp_acknum);
		          	free(new_data);
		          	free(new_data7);

				}
			}
		}
		

		// sono qui all'inizio dell'operazione di upload per mandare l'ok al client per iniziare l'operazione di upload
		if(strlen(buffer1) == 0 && tmp->ok_phase == 1){

			//////////////////////////////////////////////////////////////////////////////
			
			begin = time(NULL);

			///////////////////////////////////////////////////////////////////////////////
			
			t_data *new_data = malloc(sizeof(t_data));
			if(new_data == NULL){
				perror("\n malloc error");
				exit(-1);
			}
			
			// popolo sequence_number del packet di risposta
			asprintf(&tmp_seq,"%d",tmp_pkt);
			strcpy(new_data->sequence_number,tmp_seq);

			// popolo l'operation_no
			strcpy(new_data->operation_no,"3\0");

			// popolo ack_number del packet di risposta
			// popola act_seq_num per sapere il valore del ack_number per la risposta
			act_seq_num = atoi(new_data_gl_upload->sequence_number) + (int)strlen(new_data_gl_upload->operation_no) + (int)strlen(new_data_gl_upload->message);  // essendo un ack per la richiesta faccio +3
			
			asprintf(&tmp_acknum, "%d", act_seq_num);
			strcpy(new_data->ack_number, tmp_acknum);

			if (getsockname(send_sock, (struct sockaddr *)&sin, &len3) != 0)
			    perror("Error on getsockname");
			  	
			port_number = ntohs(sin.sin_port);
			asprintf(&new_port, "%d", port_number);
			strcpy(new_data->port, new_port);
			
			//file non presente nel server --> mando stringa di default al client
			memset(new_data->message, 0, MAXLINE);
			strcpy(new_data->message, "K");

			tmp_pkt = tmp_pkt + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);
			tmp->sequence_number = tmp_pkt;
			tmp->ack_number = act_seq_num;

			loss_p = float_rand(0.0, 1.0);
           
            if(loss_p >= LOSS_PROBABILITY){
            	if((sendto(send_sock, new_data, 1500, 0,(struct sockaddr*)&cliaddr2, sizeof(cliaddr2))) < 0){
					perror("Errore sendto\n");
					exit(1);
				}
            }

            //tmp->flag_last_ack_sent = 1;

            tmp->flag_upload_wait = 1;

	        ret_timer = srtSchedule(tmp->sockfd, tmp->timerid,new_data->sequence_number,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit, new_data->port,new_data->size);
			
#ifdef ADPTO
			append_timer_adaptive(ret_timer, new_data->sequence_number, &tmp->timer_list_client, new_data->SYNbit, new_data->ACKbit, new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,tmp->sockfd, new_data->port,new_data->size, tmp->rto, 0.0, 0.0, 0);
#else
			append_timer(ret_timer, new_data->sequence_number, &tmp->timer_list_client, new_data->SYNbit, new_data->ACKbit, new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,tmp->sockfd, new_data->port,new_data->size);			
#endif			
			tmp->timerid++;
			
			start_write = 1;
			tmp->ok_phase = 0;
			if(file_up == NULL){
				tmp->create_file = 1;
			}
			
			strcpy(buffer1, "cont esistente");
			
			tmp->expected_next_seq_number = atoi(new_data_gl_upload->sequence_number);

			// teoricamente qui dovremmo stare a gestire sempre forse una richiesta e quindi ciò prevederebbe un +3
			if(tmp->var_req == 0){
				tmp->expected_next_seq_number += 3;
				tmp->var_req = 1;
		
			}else{
				
				tmp->expected_next_seq_number = act_seq_num;
			}

			//tmp->expected_next_seq_number = expected_next_seq_num;
			free(tmp_seq);
			free(tmp_acknum);
			free(new_data);
			
			
		}
	
	} 
}


// thread il cui scopo è quello di gestire la ricezione degli ack relativi ai pacchetti inviati durante l'operazione di list
void *ack_list_handler(void *client){

		t_client *tmp = (t_client *)client;
		struct sockaddr_in cliaddr1;
		struct sembuf oper,oper_first_pkt;
		struct sembuf oper_delete,oper_sum;
		struct sembuf oper_window;
		int last_ack;
		int len1;
		t_tmr *elem;
		t_tmr *elem2;
		int num_of_files;
		struct dirent **namelist;
		int first_ack_list_received = 0;
		int round = 0;
	    int ret_val;
	    int ret, n;
	    int help_exp = 0;
	    struct sockaddr_in sin;
	    socklen_t len3;
	    
		int flag_cycle = 0;  // flag to enable to escape from the cumulative ack loop
	    
	    num_of_files = scandir("FILES_SERVER/", &namelist, file_select, alphasort);
		if (num_of_files == -1) {
    		perror("scandir");
    		exit(EXIT_FAILURE);
		}

		last_ack = tmp->last_ack;
		len1 = sizeof(cliaddr1);
		memset((void *)&cliaddr1, 0, sizeof(cliaddr1));

		while(1){

			t_data *new_data = malloc(sizeof(t_data));
			if(new_data == NULL){
				perror("\nmalloc error");
				exit(-1);
			}

			oper.sem_num = 0;
	      	oper.sem_op = -1;
	      	oper.sem_flg = 0;

re_wait_t6:
	      	//wait
	      	if((semop(tmp->semid_client, &oper, 1)) == -1){
	         	perror("Error waiting t6\n");
	         	if(errno == EINTR){
	         		goto re_wait_t6;
	         	}
	         	exit(-1);
	      	}
			
			len3 = sizeof(sin);

			if (getsockname(tmp->sock_list, (struct sockaddr *)&sin, &len3) != 0)
	              perror("Error on getsockname");
	  	
etiquette:	
			
			n = recvfrom(tmp->sock_list, new_data, 1500, 0, (struct sockaddr*)&cliaddr1, (socklen_t *)&len1);

			if(n < 1){
				perror("recvfrom error");
				if(errno == EINTR){
					goto etiquette;
				}
				exit(-1);
			}
			
			if(n == 0){
				if(close(udpfd) == -1){
					perror("errore in close");
					fflush(stdout);
					exit(1);
				}
			}

			oper_delete.sem_num = 0;
	      	oper_delete.sem_op = -1;
	      	oper_delete.sem_flg = 0;


re_waiting9:

	      	//wait in caso di retrasmit
	      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
	         	perror("Error waiting 9\n");
	         	if(errno == EINTR){
	         		goto re_waiting9;
	         	}
	         	exit(-1);
	      	}


			//controllo se il pacchetto ricevuto è presente tra quelli che devono ricevere ack
			for(elem = tmp->timer_list_client; elem != NULL; elem = elem->next){
				
				if((atoi(new_data->ack_number) == ((atoi(elem->sequence_number) + (int)strlen(elem->message) + (int)strlen(elem->operation_no)))) && (elem->sockfd == tmp->sockfd)){
					if(last_ack != atoi(new_data->ack_number)){
						if((last_ack < atoi(new_data->ack_number)) && ((last_ack + (int)strlen(elem->operation_no) + (int)strlen(elem->message)) == atoi(new_data->ack_number))){
							
							// ricezione di un ack in ordine
#ifdef ADPTO
							ret = timer_gettime((tmp->timer_list_client)->idTimer, &ltr);
							if(ret == -1){
								perror("\n timer_gettime error");
								exit(-1);
							}

							po = 1/(pow(10,9));
							sample_int = ltr.it_value.tv_sec;
							sample_dec = (float)ltr.it_value.tv_nsec *po;

							tmp->sample_rtt = (tmp->timer_list_client)->sample_value + ((tmp->timer_list_client)->last_rto - (float)(sample_int + sample_dec));
							tmp->estimated_rtt = (1 - ALPHA)*tmp->estimated_rtt + ALPHA*tmp->sample_rtt;

							if(tmp->first_to == 0){
								tmp->dev = tmp->sample_rtt/2;
							}else{
								tmp->dev = (1 - BETA)*tmp->dev + BETA*abs(tmp->sample_rtt - tmp->estimated_rtt);
							}

							int dummy = tmp->estimated_rtt + 4*tmp->dev;

							if(dummy > MIN_TIMEO && dummy < MAX_TIMEO){
								tmp->rto = dummy;
							}else if(dummy <= MIN_TIMEO){
								tmp->rto = MIN_TIMEO;
							}

#endif
							// ricevo primo ack su un pkt
							// last_ack < new_data->ack_number --> caso normale
							if(tmp->first_pkt_sent == 1){
								
								oper_first_pkt.sem_num = 0;
								oper_first_pkt.sem_op = 1;
								oper_first_pkt.sem_flg = 0;

resignal_f16:							  	  
								//wait sul valore del numero files
								if((semop(tmp->semid_fileno, &oper_first_pkt, 1)) == -1){
									perror("Error signaling 9\n");
									if(errno == EINTR){
										goto resignal_f16;
									}
									exit(-1);
								}

							}

		  					ret = timer_delete(elem->idTimer);
		  				    
		  					if(ret == -1){
		  						if(errno == EINVAL){
		  							perror("\neinval error");
		  							exit(-1);
		  						}

		    					perror("\ntimer_delete error");
		    					exit(-1);
		  					}

		  					// eliminazione dalle varie strutture dai delle informazioni salvate (timer, pacchetti inviati)
		  					delete_sent_packet(elem->sequence_number, tmp->sockfd, &tmp->list_sent_pkt);
		  					delete_id_timer(elem->idTimer, tmp->sockfd, &tmp->timer_id_list_client);
		  					delete_timer(elem->idTimer, tmp->sockfd, &tmp->timer_list_client);

		  					//print_list_tmr(tmp->timer_list_client);
		  					

		  					// azzero il numero di ack dup ricevuti quando ricevo un pacchetto in ordine
		  					help_exp += 3;

		  					oper_sum.sem_num = 0;
					      	oper_sum.sem_op = -1;
					      	oper_sum.sem_flg = 0;

rewait_f17:

					      	//signal in caso di retrasmit
					      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
					         	perror("Error waiting 10\n");
					         	if(errno == EINTR){
					         		goto rewait_f17;
					         	}
					         	exit(-1);
					      	}

		  					tmp->sum_ack = 0;

		  					oper_sum.sem_num = 0;
					      	oper_sum.sem_op = 1;
					      	oper_sum.sem_flg = 0;

resignal_f18:

					      	//signal in caso di retrasmit
					      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
					         	perror("Error waiting 11\n");
					         	if(errno == EINTR){
					         		goto resignal_f18;
					         	}
					         	exit(-1);
					      	}

		  					if(tmp->ack_received > 0){
		  						first_ack_list_received = 1;
		  					}

		  					tmp->ack_received++;
		  					last_ack = atoi(new_data->ack_number);
		  					tmp->last_ack = last_ack;

		  					//se ho ricevuto tutti gli acks allora --> pthread_exit((void *)&ret_val);
		  					
		  					if(tmp->first_pkt_sent == 0){

			  					oper_window.sem_num = 0;
						      	oper_window.sem_op = 1;
						      	oper_window.sem_flg = 0;

resignal_l:

						      	//signal in caso di retrasmit
						      	if((semop(tmp->semid_window, &oper_window, 1)) == -1){
						         	perror("Error waiting 11\n");
						         	if(errno == EINTR){
						         		goto resignal_l;
						         	}
						         	exit(-1);
						      	}
					        }

					      	if(tmp->first_pkt_sent == 1){
					      		tmp->first_pkt_sent = 0;
					      	}
		  					
							if(tmp->ack_received == num_of_files + 1){
								
								tmp->receiving_ack_phase = 0;
								tmp->ack_received = 0;
								free(new_data);
								tmp->expected_next_seq_number += help_exp;
								tmp->retr_phase = 0;
								tmp->first_pkt_sent = 0;
								tmp->sum_ack = 0;

								//expected_next_seq_num += (num_of_files + 1)*3;
								ret_val = 666;

								oper_delete.sem_num = 0;
						      	oper_delete.sem_op = 1;
						      	oper_delete.sem_flg = 0;
resignal_f19:
						      	//signal in caso di retrasmit
						      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
						         	perror("Error waiting 12\n");
						         	if(errno == EINTR){
						         		goto resignal_f19;
						         	}
						         	exit(-1);
						      	}
								pthread_exit((void *)&ret_val);
							}else{
								break;
							}
						}else{

							// ricezione di un ack fuori ordine

							//1. if((last_ack < atoi(new_data->ack_number)) && ((last_ack + strlen(elem->operation_no) + strlen(elem->message)) != atoi(new_data->ack_number))){
							//1. ---> abbiamo ricevuto un nuovo ack ma non in ordine ---> ack cumulativo
							if((last_ack < atoi(new_data->ack_number)) && ((last_ack + (int)strlen(elem->operation_no) + (int)strlen(elem->message)) != atoi(new_data->ack_number)) && (elem->sockfd == tmp->sockfd)){
								if((last_ack + (int)strlen(elem->operation_no) + (int)strlen(elem->message)) < atoi(new_data->ack_number)){
									
							      	//print_list_tmr(tmp->timer_list_client);

							      	last_ack = atoi(new_data->ack_number);
						  			tmp->last_ack = last_ack;

						  			oper_sum.sem_num = 0;
							      	oper_sum.sem_op = -1;
							      	oper_sum.sem_flg = 0;
rewait_f20:
							      	//signal in caso di retrasmit
							      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
							         	perror("Error waiting 10\n");
							         	if(errno == EINTR){
							         		goto rewait_f20;
							         	}
							         	exit(-1);
							      	}

				  					tmp->sum_ack = 0;

				  					oper_sum.sem_num = 0;
							      	oper_sum.sem_op = 1;
							      	oper_sum.sem_flg = 0;

resignal_f21:
							      	//signal in caso di retrasmit
							      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
							         	perror("Error waiting 11\n");
							         	if(errno == EINTR){
							         		goto resignal_f21;
							         	}
							         	exit(-1);
							      	}


									
#ifdef ADPTO
									ret = timer_gettime((tmp->timer_list_client)->idTimer, &ltr);
									if(ret == -1){
										perror("\n timer_gettime error");
										exit(-1);
									}

									po = 1/(pow(10,9));
									sample_int = ltr.it_value.tv_sec;
									sample_dec = (float)ltr.it_value.tv_nsec *po;

									tmp->sample_rtt = (tmp->timer_list_client)->sample_value + ((tmp->timer_list_client)->last_rto - (float)(sample_int + sample_dec));
									tmp->estimated_rtt = (1 - ALPHA)*tmp->estimated_rtt + ALPHA*tmp->sample_rtt;

									if(tmp->first_to == 0){
										tmp->dev = tmp->sample_rtt/2;
									}else{
										tmp->dev = (1 - BETA)*tmp->dev + BETA*abs(tmp->sample_rtt - tmp->estimated_rtt);
									}

									int dummy = tmp->estimated_rtt + 4*tmp->dev;

									if(dummy > MIN_TIMEO && dummy < MAX_TIMEO){
										tmp->rto = dummy;
									}else if(dummy <= MIN_TIMEO){
										tmp->rto = MIN_TIMEO;
									}

#endif


									for(elem2 = tmp->timer_list_client; elem2 != NULL; elem2 = elem2->next){
										if(elem2->sockfd == tmp->sockfd){
											//print_list_tmr(tmp->timer_list_client);
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

							  					delete_sent_packet(elem2->sequence_number, tmp->sockfd, &tmp->list_sent_pkt);
							  					delete_id_timer(elem2->idTimer, tmp->sockfd, &tmp->timer_id_list_client);
							  					delete_timer(elem2->idTimer, tmp->sockfd, &tmp->timer_list_client);
							  					
							  					help_exp += 3;

							  					oper_window.sem_num = 0;
										      	oper_window.sem_op = 1;
										      	oper_window.sem_flg = 0;
resignal_l2:
										      	//signal in caso di retrasmit
										      	if((semop(tmp->semid_window, &oper_window, 1)) == -1){
										         	perror("Error waiting 11\n");
										         	if(errno == EINTR){
										         		goto resignal_l2;
										         	}
										         	exit(-1);
										      	}

							  					tmp->ack_received++;
							  					//print_list_tmr(tmp->timer_list_client);

							  					if(flag_cycle == 1){
							  						flag_cycle = 0;
							  						break;
							  					}
							  					else{
							  						continue;
							  					}

											}

						  					//se ho ricevuto tutti gli acks allora --> pthread_exit((void *)&ret_val);
						  					
											if(tmp->ack_received == num_of_files + 1){
												
												tmp->receiving_ack_phase = 0;
												tmp->ack_received = 0;
												free(new_data);
												tmp->retr_phase = 0;
												tmp->expected_next_seq_number += help_exp;
												tmp->sum_ack = 0;
												tmp->first_pkt_sent = 0;
												//expected_next_seq_num += (num_of_files + 1)*3;
												oper_delete.sem_num = 0;
										      	oper_delete.sem_op = 1;
										      	oper_delete.sem_flg = 0;

resignal_f22:

										      	//signal dopo aver eventualmente eliminato timer multipli
										      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
										         	perror("Error waiting 14\n");
										         	if(errno == EINTR){
										         		goto resignal_f22;
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
					  				
									}

									if(tmp->ack_received == num_of_files + 1){
										
										tmp->receiving_ack_phase = 0;
										tmp->ack_received = 0;
										free(new_data);
										tmp->retr_phase = 0;
										tmp->expected_next_seq_number += help_exp;
										tmp->sum_ack = 0;
										tmp->first_pkt_sent = 0;
										//expected_next_seq_num += (num_of_files + 1)*3;
										
										oper_delete.sem_num = 0;
								      	oper_delete.sem_op = 1;
								      	oper_delete.sem_flg = 0;
resignal_f23:
								      	//signal dopo aver eventualmente eliminato timer multipli
								      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
								         	perror("Error waiting 15\n");
								         	if(errno == EINTR){
								         		goto resignal_f23;
								         	}
								         	exit(-1);
								      	}

								      	ret_val = 666;
										pthread_exit((void *)&ret_val);
									}else{
										
									}
									
									
							    }
								
							}
						
						}
	  					
	  					
					}else{  //sto ricevendo duplicati
						
						if(elem->sockfd == tmp->sockfd){
							if(last_ack == atoi(new_data->ack_number)){
								
								oper_sum.sem_num = 0;
						      	oper_sum.sem_op = -1;
						      	oper_sum.sem_flg = 0;

rewait_f24:
						      	//wait su semaforo per ack cumulativo
						      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
						         	perror("Error waiting 17\n");
						         	if(errno == EINTR){
						         		goto rewait_f24;
						         	}
						         	exit(-1);
						      	}

								tmp->sum_ack++;
								help_exp += 3;

								// handler tre pacchetti duplicati
								if(tmp->sum_ack == 3){
									// retransmit last ack correctly sent
									three_duplicate_message(new_data->ack_number, tmp->sockfd);
									tmp->sum_ack = 0; 
								}

								oper_sum.sem_num = 0;
						      	oper_sum.sem_op = 1;
						      	oper_sum.sem_flg = 0;

resignal_f25:
						      	//signal su semaforo per ack cumulativo
						      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
						         	perror("Error waiting 17\n");
						         	if(errno == EINTR){
						         		goto resignal_f25;
						         	}
						         	exit(-1);
						      	}
							}
						}
						
	 					break;

					}	
			
				}else{

					if(elem->sockfd == tmp->sockfd){
						//probabilmente ack duplicato
						
						if(last_ack == atoi(new_data->ack_number)){
							
							oper_sum.sem_num = 0;
					      	oper_sum.sem_op = -1;
					      	oper_sum.sem_flg = 0;

rewait_f26:

					      	//wait su semaforo per ack cumulativo
					      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
					         	perror("Error waiting 17\n");
					         	if(errno == EINTR){
					         		goto rewait_f26;
					         	}
					         	exit(-1);
					      	}

							tmp->sum_ack++;
							help_exp += 3;

							// handler tre pacchetti duplicati
							if(tmp->sum_ack == 3){
								// retransmit last ack correctly sent
								three_duplicate_message(new_data->ack_number, tmp->sockfd);
								tmp->sum_ack = 0; 
							}

							oper_sum.sem_num = 0;
					      	oper_sum.sem_op = 1;
					      	oper_sum.sem_flg = 0;

resignal_f27:
					      	//signal su semaforo per ack cumulativo
					      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
					         	perror("Error waiting 17\n");
					         	if(errno == EINTR){
					         		goto resignal_f27;
					         	}
					         	exit(-1);
					      	}

					      	break;
					    }
					}
					
					
				}

			}

			oper_delete.sem_num = 0;
	      	oper_delete.sem_op = 1;
	      	oper_delete.sem_flg = 0;

resignal_f28:

	      	//signal in caso di retrasmit
	      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
	         	perror("Error waiting 12\n");
	         	if(errno == EINTR){
	         		goto resignal_f28;
	         	}
	         	exit(-1);
	      	}
		}
		
    	
}

// thread che si occupa della trasmissione verso il client dei files disponibili presso la directory del server
void *list_files(void *s_sock){

	int act_seq_num;
	char *tmp_seq;
	int  tmp_pkt;
	char *tmp_acknum;
	int last_ack;
	struct dirent **namelist;
    int n, ret_val1, ret;
    void *ret_val;
    timer_t ret_timer;
    int send_sock = *((int *)&s_sock);
    t_client 	*tmp;
    struct sockaddr_in cliaddr1;
    int index;
    int expected_next_seq_num;
    int port;
    char *new_port;
    struct sembuf oper;
    char *value;
    char *tmp_port_d;
    int port_d;
    int sockfd_ack_list;
    struct sockaddr_in myNewAddr,sin;
    int sockfd_filename_download;
    struct sockaddr_in myNewAddrAckDownload,sin2;
    float loss_p;
    struct sembuf oper_timer;
    struct sembuf oper_sum,oper_first_pkt;
    struct sembuf oper_window;

	printf("start listing files in directory...\n\n\n");
	
	printf("\t**** AVAILABLE FILES ****\n\n\n");
	system("cd FILES_SERVER/; ls");
	
	n = scandir("FILES_SERVER/", &namelist, file_select, alphasort);
	if (n == -1) {
		perror("scandir");
		exit(EXIT_FAILURE);
	}

	asprintf(&value, "%d", n);

	//////////////////////////////////////////////////////////////////////////////
	time_t begin = time(NULL);

	///////////////////////////////////////////////////////////////////////////////

	for (tmp=list_head; tmp!=NULL; tmp=tmp->next){
		if(tmp->sockfd == send_sock){
	 		// verifico che la porta del client sia corretta
			//send_port = ntohs(cliaddr.sin_port);
			cliaddr1.sin_port = htons(tmp->port);
			inet_aton(tmp->ipaddr, &cliaddr1.sin_addr);
			cliaddr1.sin_family = AF_INET;
			tmp_pkt = tmp->sequence_number;
			if(tmp->expected_next_seq_number != 0){
				expected_next_seq_num = tmp->expected_next_seq_number;
			}

			last_ack = tmp->last_ack;
			break;
		}	
	}

  	

	oper_timer.sem_num = 0;
   	oper_timer.sem_op = -1;
  	oper_timer.sem_flg = 0;

rewait_f29:

  	//wait sul valore timer
  	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
     	perror("Error waiting su timer di controllo\n");
     	if(errno == EINTR){
     		goto rewait_f29;
     	}
     	exit(-1);
  	}

	tmp->master_timer = 1;
	//////////////////////////////////////////////////////////////////////////////////////////////////////////////////
	ret_timer = srtScheduleS(tmp->sockfd, tmp->timerid);
	tmp->timerid++;

	//append non dovrebbe servire

	tmp->master_timer = 0;
	tmp->master_exists_flag = 1;
	
	oper_timer.sem_num = 0;
   	oper_timer.sem_op = 1;
  	oper_timer.sem_flg = 0;

resignal_f30:

  	//wait sul valore timer
  	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
     	perror("Error signaling su timer di controllo\n");
     	if(errno == EINTR){
     		goto resignal_f30;
     	}
     	exit(-1);
  	}

	sockfd_ack_list = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd_ack_list < 0){
        perror("\nsocket error");
        exit(-1);
    }

    memset((void *)&myNewAddr, 0, sizeof(myNewAddr));
    myNewAddr.sin_family = AF_INET; 
    myNewAddr.sin_addr.s_addr = htonl(INADDR_ANY); 
    myNewAddr.sin_port = htons(0); 
    if(bind(sockfd_ack_list, (struct sockaddr*)&myNewAddr, sizeof(myNewAddr)) < 0){
    	perror("\n bind error on binding for thread ack list\n");
    	exit(-1);
    }

    // salvo il nuovo sock id relativo ad un certo client
    tmp->sock_list = sockfd_ack_list;
    socklen_t len3 = sizeof(sin);

    if (getsockname(sockfd_ack_list, (struct sockaddr *)&sin, &len3) != 0)
	    perror("Error on getsockname");
	  	
	port = ntohs(sin.sin_port);

	ret = pthread_create(&tid_ack_list, NULL, ack_list_handler, (void *)tmp);
    if(ret != 0){
        perror("pthread_create error");
        exit(1);
    }

    // ******* inizio invio del numero di files da gestire da parte del client **********
	
	t_data *new_data = malloc(sizeof(t_data));
    if(new_data == NULL){
    	perror("malloc error");
    	exit(1);
    }
	
	// popolo sequence_number del packet di risposta
	asprintf(&tmp_seq,"%d",tmp_pkt);
	strcpy(new_data->sequence_number,tmp_seq);

	if(tmp->download_list_phase == 1){
		//oltre a mandare pkt con numero di file dico al client dove dovrà mandare il nome del file che ha scelto da scaricare
		strcpy(new_data->operation_no,"2\0");
		act_seq_num = atoi(new_data_gl_download->sequence_number) + (int)strlen(new_data_gl_download->message) + (int)strlen(new_data_gl_download->operation_no);	

		sockfd_filename_download = socket(AF_INET, SOCK_DGRAM, 0);
	    if(sockfd_filename_download < 0){
	        perror("\nsocket error");
	        exit(-1);
	    }

	    memset((void *)&myNewAddrAckDownload, 0, sizeof(myNewAddrAckDownload));
	    myNewAddrAckDownload.sin_family = AF_INET; 
	    myNewAddrAckDownload.sin_addr.s_addr = htonl(INADDR_ANY); 
	    myNewAddrAckDownload.sin_port = htons(0); 
	    if(bind(sockfd_filename_download, (struct sockaddr*)&myNewAddrAckDownload, sizeof(myNewAddrAckDownload)) < 0){
	    	perror("\n bind error on binding for thread ack list\n");
	    	exit(-1);
	    }

	    socklen_t len4 = sizeof(sin2);

	    if (getsockname(sockfd_filename_download, (struct sockaddr *)&sin2, &len4) != 0)
		    perror("Error on getsockname");
		  	
		port_d = ntohs(sin2.sin_port);

		tmp->port_download = port_d;
		tmp->sock_filename_download = sockfd_filename_download;
		asprintf(&tmp_port_d, "%d", port_d);
		strcpy(new_data->port_download, tmp_port_d);

	}else{
		tmp->operation_value = 1;
		strcpy(new_data->operation_no,"1\0");
		act_seq_num = atoi(new_data_gl_list->sequence_number) + (int)strlen(new_data_gl_list->message) + (int)strlen(new_data_gl_list->operation_no);
	}
	
	tmp->ack_number = act_seq_num;
	// popolo ack_number del packet di risposta
	//popola act_seq_num per sapere il valore del ack_number per la risposta
	

	asprintf(&tmp_acknum, "%d", act_seq_num);
	strcpy(new_data->ack_number, tmp_acknum);

	asprintf(&new_port, "%d", port);
	strcpy(new_data->port, new_port);
	// data
	memset(new_data->message, 0, sizeof(new_data->message));
	strcpy(new_data->message, value);
	tmp_pkt = tmp_pkt + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);
	tmp->sequence_number = tmp_pkt;
	
    oper_timer.sem_num = 0;
  	oper_timer.sem_op = -1;
  	oper_timer.sem_flg = 0;

rewait_f31:

  	//wait sul valore timer
  	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
     	perror("Error signaling 11\n");
     	if(errno == EINTR){
     		goto rewait_f31;
     	}
     	exit(-1);
  	}

    oper_sum.sem_num = 0;
  	oper_sum.sem_op = -1;
  	oper_sum.sem_flg = 0;

  	//wait
  	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
     	perror("Error waiting 17\n");
     	exit(-1);
  	}

	ret_timer = srtSchedule(tmp->sockfd, tmp->timerid, new_data->sequence_number,new_data->SYNbit,new_data->ACKbit,new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit, new_data->port,new_data->size);
	
#ifdef ADPTO
	append_timer_adaptive(ret_timer, new_data->sequence_number, &tmp->timer_list_client, new_data->SYNbit, new_data->ACKbit, new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,tmp->sockfd, new_data->port,new_data->size, tmp->rto, 0.0, 0.0, 0);
#else
	append_timer(ret_timer, new_data->sequence_number, &tmp->timer_list_client, new_data->SYNbit, new_data->ACKbit, new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,tmp->sockfd, new_data->port,new_data->size);	
#endif

	append_sent_packets(new_data->sequence_number, new_data->SYNbit, new_data->ACKbit, new_data->ack_number,new_data->message,new_data->operation_no,new_data->FINbit,tmp->sockfd, new_data->port, &tmp->list_sent_pkt,new_data->size);
	
  	loss_p = float_rand(0.0, 1.0);
  	// il numero di files lo mandiamo sempre momentaneamente
	if(loss_p > LOSS_PROBABILITY){
		if((sendto(send_sock, new_data, 1500, 0, (struct sockaddr*)&cliaddr1,(socklen_t)sizeof(cliaddr1))) < 0){
			perror("Errore sendto\n");
			exit(1);
		} 
	}

	tmp->first_pkt_sent = 1;
	tmp->expected_next_seq_number += 3;
	tmp->timerid++;
	
	oper_timer.sem_num = 0;
  	oper_timer.sem_op = 1;
  	oper_timer.sem_flg = 0;

resignal_f32:

  	//signal sul valore del timer
  	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
     	perror("Error signaling 12\n");
     	if(errno == EINTR){
     		goto resignal_f32;
     	}
     	exit(-1);
  	}


	oper_sum.sem_num = 0;
  	oper_sum.sem_op = 1;
  	oper_sum.sem_flg = 0;

resignal_f33:

  	//signal
  	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
     	perror("Error waiting 18\n");
     	if(errno == EINTR){
     		goto resignal_f33;
     	}
     	exit(-1);
  	}

	
	oper.sem_num = 0;
  	oper.sem_op = 1;
  	oper.sem_flg = 0;

resignal_f34:

  	//signal
  	if((semop(tmp->semid_client, &oper, 1)) == -1){
     	perror("Error signaling 13\n");
     	if(errno == EINTR){
     		goto resignal_f34;
     	}
     	exit(-1);
  	}

	// perché il client una volta ricevuto il pkt dovrà mandare un ack verso il server 
	expected_ack = expected_ack + (int)strlen(new_data->message) + (int)strlen(new_data->operation_no);
	free(new_data);
	
	// *******  fine invio del numero di files verso il client *********

    oper_first_pkt.sem_num = 0;
	oper_first_pkt.sem_op = -1;
	oper_first_pkt.sem_flg = 0;
	  

re_waiting21:

	//wait sul valore del numero files
	if((semop(tmp->semid_fileno, &oper_first_pkt, 1)) == -1){
		perror("Error waiitng 21\n");
		if(errno == EINTR){
			goto re_waiting21;
		}
		
		exit(-1);
	}



	while (n--) {

		//  ******* invio di un file verso il client ***********

		oper_window.sem_num = 0;
      	oper_window.sem_op = -1;
      	oper_window.sem_flg = 0;

rewait_l:

      	//wait sul valore timer
      	if((semop(tmp->semid_window, &oper_window, 1)) == -1){
         	perror("Error signaling 15 (1)\n");
         	if(errno == EINTR){
         		goto rewait_l;
         	}
         	exit(-1);
      	}

		t_data *new_data_file = malloc(sizeof(t_data));
	    if(new_data_file == NULL){
	    	perror("malloc error");
	    	exit(1);
	    }
    	

		index = n; // indice per sapere gli elementi da inviare in caso non avessimo spazio in finestra

		// popolo sequence_number del packet di risposta
		tmp_pkt = tmp->sequence_number;
		asprintf(&tmp_seq,"%d",tmp_pkt);
		strcpy(new_data_file->sequence_number,tmp_seq);

		if(tmp->download_list_phase == 1){
			strcpy(new_data_file->operation_no,"2\0");
			act_seq_num = atoi(new_data_gl_download->sequence_number) + (int)strlen(new_data_gl_download->message) + (int)strlen(new_data_gl_download->operation_no);
		}else{
			strcpy(new_data_file->operation_no,"1\0");
			act_seq_num = atoi(new_data_gl_list->sequence_number) + (int)strlen(new_data_gl_list->message) + (int)strlen(new_data_gl_list->operation_no);
		}
		// popolo ack_number del packet di risposta
	
		asprintf(&tmp_acknum, "%d", act_seq_num);
		strcpy(new_data_file->ack_number, tmp_acknum);

		asprintf(&new_port, "%d", port);
		strcpy(new_data_file->port, new_port);

		strcpy(new_data_file->message, namelist[n]->d_name);
	
		tmp_pkt = tmp_pkt + (int)strlen(new_data_file->message) + (int)strlen(new_data_file->operation_no);
		tmp->sequence_number = tmp_pkt;
		tmp->ack_number = act_seq_num;

      	oper_timer.sem_num = 0;
      	oper_timer.sem_op = -1;
      	oper_timer.sem_flg = 0;

rewait_t15:

      	//wait sul valore timer
      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error waiting 15\n");
         	if(errno == EINTR){
         		goto rewait_t15;
         	}
         	exit(-1);
      	}

      	oper_sum.sem_num = 0;
      	oper_sum.sem_op = -1;
      	oper_sum.sem_flg = 0;

rewait_f35:
      	//wait
      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
         	perror("Error waiting 19\n");
         	if(errno == EINTR){
         		goto rewait_f35;
         	}
         	exit(-1);
      	}

		ret_timer = srtSchedule(tmp->sockfd,tmp->timerid,new_data_file->sequence_number,new_data_file->SYNbit,new_data_file->ACKbit,new_data_file->ack_number,new_data_file->message,new_data_file->operation_no,new_data_file->FINbit, new_data_file->port,new_data_file->size);
		
#ifdef ADPTO
		append_timer_adaptive(ret_timer, new_data_file->sequence_number, &tmp->timer_list_client, new_data_file->SYNbit, new_data_file->ACKbit, new_data_file->ack_number,new_data_file->message,new_data_file->operation_no,new_data_file->FINbit,tmp->sockfd, new_data_file->port,new_data_file->size, tmp->rto, 0.0, 0.0, 0);
#else
		append_timer(ret_timer, new_data_file->sequence_number, &tmp->timer_list_client, new_data_file->SYNbit, new_data_file->ACKbit, new_data_file->ack_number,new_data_file->message,new_data_file->operation_no,new_data_file->FINbit,tmp->sockfd, new_data_file->port,new_data_file->size);		
#endif

		append_sent_packets(new_data_file->sequence_number, new_data_file->SYNbit, new_data_file->ACKbit, new_data_file->ack_number,new_data_file->message,new_data_file->operation_no,new_data_file->FINbit,tmp->sockfd, new_data_file->port, &tmp->list_sent_pkt,new_data_file->size);
		
		loss_p = float_rand(0.0, 1.0);
		
		if(loss_p >= LOSS_PROBABILITY){
			if((sendto(send_sock, new_data_file, 1500, 0, (struct sockaddr*)&cliaddr1, sizeof(cliaddr1))) < 0){
				perror("Errore sendto\n");
				exit(1);
			}
		}
		
		tmp->expected_next_seq_number = act_seq_num;
		tmp->timerid++;
		
		oper_timer.sem_num = 0;
      	oper_timer.sem_op = 1;
      	oper_timer.sem_flg = 0;

resignal_f36:

      	//signal sul valore timer
      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error signaling 16\n");
         	if(errno == EINTR){
         		goto resignal_f36;
         	}
         	exit(-1);
      	}

		oper_sum.sem_num = 0;
      	oper_sum.sem_op = 1;
      	oper_sum.sem_flg = 0;

resignal_f37:

      	//signal
      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
         	perror("Error waiting 20\n");
         	if(errno == EINTR){
         		goto resignal_f37;
         	}
         	exit(-1);
      	}

		
		oper.sem_num = 0;
      	oper.sem_op = 1;
      	oper.sem_flg = 0;

resignal_f38:

      	//signal
      	if((semop(tmp->semid_client, &oper, 1)) == -1){
         	perror("Error signaling 17\n");
         	if(errno == EINTR){
         		goto resignal_f38;
         	}
         	exit(-1);
      	}
		// perché il client una volta ricevuto il pkt dovrà mandare un ack verso il server 
		expected_ack = expected_ack + (int)strlen(new_data_file->message) + (int)strlen(new_data_file->operation_no);
		free(new_data_file);
	    	
    	// **********  invio di un file verso il client *********

	} 	

	
    ret_val = malloc(sizeof(int));
	pthread_join(tid_ack_list, &ret_val);
	
	if(tmp->master_exists_flag == 1){
		ret = timer_delete(tmp->master_IDTimer);	  				    
		if(ret == -1){
			perror("\ntimer_delete error");
			exit(-1);
		}

		tmp->master_exists_flag = 0;
	}
	

    printf("\n -----------> Task list completed... Bye!");
	fflush(stdout);

	//print_list_sent_pkt(tmp->list_sent_pkt);
	ret_val1 = 666;

	/////////////////////////////////////////////////////////////////////////////
	time_t end = time(NULL);

	printf("\n TIME: %ju sec", (uintmax_t)(end - begin));
	fflush(stdout);
	/////////////////////////////////////////////////////////////////////////////

	pthread_exit((void *)&ret_val1);
   

}


// thread che si occupa della gestione della ricezione degli ack in caso richiesta di downlaod da parte del client
void *ack_download_handler(void *client){

	t_client *tmp = (t_client*)client; 
	int help_d_exp = 0;
	int len1, n, ret;
	struct sockaddr_in cliaddr1;
	struct sembuf oper,oper2, oper_first_pkt, oper_delete, oper_sum,oper_timer;
	struct sembuf oper_window;
	t_tmr *elem, *elem2;
	int first_ack_list_received = 0;
	int ret_val;
	int last_ack;
	int round = 0;
	int flag_cycle = 0; 
	mask_sig();
	
    last_ack = tmp->last_ack;

 	
    while(1){
		
		len1 = sizeof(cliaddr1);

		t_data *new_data = malloc(sizeof(t_data));
		if(new_data == NULL){
			perror("malloc error");
			exit(1);
		}

		oper.sem_num = 0;
      	oper.sem_op = -1;
      	oper.sem_flg = 0;

rewait_f39:

      	//wait
      	if((semop(tmp->semid_client, &oper, 1)) == -1){
      		perror("\nsemop error sss");
         	if(errno == EINTR){
         		goto rewait_f39;
         	}
         	exit(-1);
      	}

rerecvfrom:

		n = recvfrom(tmp->sock_download, new_data, 1500, 0, (struct sockaddr*)&cliaddr1, (socklen_t *)&len1);
		if(n == -1){
			perror("\n recvfrom error (43)");
			if(errno == EINTR){
    			goto rerecvfrom;
    		}

    		exit(-1);
		}


		if(n == 0){
			if(close(udpfd) == -1){
				perror("errore in close");
				exit(1);
			}
		}

		oper_delete.sem_num = 0;
      	oper_delete.sem_op = -1;
      	oper_delete.sem_flg = 0;

rewaiting_timer:

      	//wait
      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
         	perror("\n semop error (34)");
         	if(errno == EINTR){
         		goto rewaiting_timer;
         	}
         	exit(-1);
      	}


		//controllo se il pacchetto ricevuto è presente tra quelli che devono ricevere ack
		for(elem = tmp->timer_list_client; elem != NULL; elem = elem->next){
			if((atoi(new_data->ack_number) == ((atoi(elem->sequence_number) + (int)strlen(elem->message) + (int)strlen(elem->operation_no)))) && (elem->sockfd == tmp->sockfd)){
					if(last_ack != atoi(new_data->ack_number)){
						if((last_ack < atoi(new_data->ack_number)) && ((last_ack + (int)strlen(elem->operation_no) + (int)strlen(elem->message)) == atoi(new_data->ack_number))){
							//ricevo primo ack su un pkt
							// last_ack < new_data->ack_number --> caso normale

							// ricezione ack in ordine

#ifdef ADPTO 
							ret = timer_gettime((tmp->timer_list_client)->idTimer, &ltr);
							if(ret == -1){
								perror("\n timer_gettime error");
								exit(-1);
							}

							po = 1/(pow(10,9));
							sample_int = ltr.it_value.tv_sec;
							sample_dec = (float)ltr.it_value.tv_nsec *po;

							tmp->sample_rtt = (tmp->timer_list_client)->sample_value + ((tmp->timer_list_client)->last_rto - (float)(sample_int + sample_dec));
							tmp->estimated_rtt = (1 - ALPHA)*tmp->estimated_rtt + ALPHA*tmp->sample_rtt;

							if(tmp->first_to == 0){
								tmp->dev = tmp->sample_rtt/2;
							}else{
								tmp->dev = (1 - BETA)*tmp->dev + BETA*abs(tmp->sample_rtt - tmp->estimated_rtt);
							}
							int dummy = tmp->estimated_rtt + 4*tmp->dev;

							if(dummy > MIN_TIMEO && dummy < MAX_TIMEO){
								tmp->rto = dummy;
							}else if(dummy <= MIN_TIMEO){
								tmp->rto = MIN_TIMEO;
							}

#endif

							if(tmp->first_pkt_sent == 1){
								
								// signal per abilitare invio del contenuto del file
								oper_first_pkt.sem_num = 0;
								oper_first_pkt.sem_op = 1;
								oper_first_pkt.sem_flg = 0;

resignal_f40:								  	  
								//wait sul valore del numero files
								if((semop(tmp->semid_fileno, &oper_first_pkt, 1)) == -1){
									perror("Error signaling 9\n");
									if(errno == EINTR){
						         		goto resignal_f40;
						         	}
									exit(-1);
								}

							}

							ret = timer_delete(elem->idTimer);
	  				    
	  						if(ret == -1){
	  							perror("\ntimer_delete error");
	  							if(errno == EINVAL){
	  								perror("\neinval error");
	  								exit(-1);
	  							}

	    						perror("\ntimer_delete error");
	    						exit(-1);
	  						}

	  						// eliminazione delle informazioni salvate nel momento dell'invio del pacchetto
	  						delete_sent_packet(elem->sequence_number, tmp->sockfd, &tmp->list_sent_pkt);
	  						delete_id_timer(elem->idTimer, tmp->sockfd, &tmp->timer_id_list_client);
	  						delete_timer(elem->idTimer, tmp->sockfd, &tmp->timer_list_client);

			  				//print_list_tmr(tmp->timer_list_client);
			  				
			  				
	  						help_d_exp += 3;  //18 tot nel caso paperino
	  						
	  						oper_sum.sem_num = 0;
							oper_sum.sem_op = -1;
							oper_sum.sem_flg = 0;

rewait_f41:  
							//wait sul valore del numero files
							if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
								perror("Error signaling 9\n");
								if(errno == EINTR){
					         		goto rewait_f41;
					         	}
								exit(-1);
							}

							tmp->sum_ack = 0; // avendo ricevuto in ordine azzero il numero di duplicati ricevuti

							oper_sum.sem_num = 0;
							oper_sum.sem_op = 1;
							oper_sum.sem_flg = 0;

resignal_f42:	  
							//wait sul valore del numero files
							if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
								perror("Error signaling 9\n");
								if(errno == EINTR){
					         		goto resignal_f42;
					         	}
								exit(-1);
							}

	  						
	  						if(tmp->ack_received > 0){
	  							first_ack_list_received = 1;
	  						}

	  						tmp->ack_received++;
	  						last_ack = atoi(new_data->ack_number);
	  						tmp->last_ack = last_ack;

	  						//se ho ricevuto tutti gli acks allora --> pthread_exit((void *)&ret_val);
	  						
	  						if(tmp->first_pkt_sent == 0){

	  							oper_window.sem_num = 0;
								oper_window.sem_op = 1;
								oper_window.sem_flg = 0;

resignal_l3:
								  	  
								//wait sul valore del numero files
								if((semop(tmp->semid_window, &oper_window, 1)) == -1){
									perror("Error signaling 9\n");
									if(errno == EINTR){
										goto resignal_l3;
									}
									exit(-1);
								}

								
	  						}

	  						if(tmp->first_pkt_sent == 1){
	  							tmp->first_pkt_sent = 0;
	  						}
	  						
	  					
							if(tmp->ack_received == tmp->file_content + 2 || (strcmp(new_data->message,"404") == 0)){
								oper2.sem_num = 0;
						      	oper2.sem_op = -1;
						      	oper2.sem_flg = 0;

rewait_f44:
						      	//wait-->sono sicuro che expected non verrà sovrascritto perchè thread download è in join
						      	if((semop(tmp->semid_expected, &oper2, 1)) == -1){
						         	perror("Error signaling 19\n");
						         	if(errno == EINTR){
						         		goto rewait_f44;
						         	}
						         	exit(-1);
						      	}
								
								tmp->file_content = 0;
								tmp->receiving_ack_phase_download = 0;
								tmp->ack_received = 0;
								tmp->file_content = 0;
								tmp->expected_next_seq_number += help_d_exp;  //nel caso pape 44
								free(new_data);
								tmp->download_list_phase = 0;

								oper_timer.sem_num = 0;
								oper_timer.sem_op = 1;
								oper_timer.sem_flg = 0;

resignal_f45:	  
								//wait sul valore del numero files
								if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
									perror("Error signaling 9\n");
									if(errno == EINTR){
										goto resignal_f45;
									}
									exit(-1);
								}

								pthread_exit((void *)&ret_val);
							}else{
								free(new_data);
								break;
							}

						}else{

							// ricezione di un ack fuori ordine

							//1. if((last_ack < atoi(new_data->ack_number)) && ((last_ack + strlen(elem->operation_no) + strlen(elem->message)) != atoi(new_data->ack_number))){
							//1. ---> abbiamo ricevuto un nuovo ack ma non in ordine ---> ack cumulativo
							if((last_ack < atoi(new_data->ack_number)) && ((last_ack + (int)strlen(elem->operation_no) + (int)strlen(elem->message)) != atoi(new_data->ack_number)) && (elem->sockfd == tmp->sockfd)){
								if((last_ack + (int)strlen(elem->operation_no) + (int)strlen(elem->message)) < atoi(new_data->ack_number)){
									
							      	//print_list_tmr(tmp->timer_list_client);

							      	last_ack = atoi(new_data->ack_number);
						  			tmp->last_ack = last_ack;

						  			oper_sum.sem_num = 0;
									oper_sum.sem_op = -1;
									oper_sum.sem_flg = 0;
rewait_f46:
			  
									//wait sul valore del numero files
									if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
										perror("Error signaling 9\n");
										if(errno == EINTR){
											goto rewait_f46;
										}
										exit(-1);
									}

									tmp->sum_ack = 0; // avendo ricevuto in ordine azzero il numero di duplicati ricevuti

									oper_sum.sem_num = 0;
									oper_sum.sem_op = 1;
									oper_sum.sem_flg = 0;

resignal_f47:
			  
									//wait sul valore del numero files
									if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
										perror("Error signaling 9\n");
										if(errno == EINTR){
											goto resignal_f47;
										}
										exit(-1);
									}


									
#ifdef ADPTO
									ret = timer_gettime((tmp->timer_list_client)->idTimer, &ltr);
									if(ret == -1){
										perror("\n timer_gettime error");
										exit(-1);
									}

									po = 1/(pow(10,9));
									sample_int = ltr.it_value.tv_sec;
									sample_dec = (float)ltr.it_value.tv_nsec *po;

									tmp->sample_rtt = (tmp->timer_list_client)->sample_value + ((tmp->timer_list_client)->last_rto - (float)(sample_int + sample_dec));
									tmp->estimated_rtt = (1 - ALPHA)*tmp->estimated_rtt + ALPHA*tmp->sample_rtt;

									if(tmp->first_to == 0){
										tmp->dev = tmp->sample_rtt/2;
									}else{
										tmp->dev = (1 - BETA)*tmp->dev + BETA*abs(tmp->sample_rtt - tmp->estimated_rtt);
									}

									int dummy = tmp->estimated_rtt + 4*tmp->dev;

									if(dummy > MIN_TIMEO && dummy < MAX_TIMEO){
										tmp->rto = dummy;
									}else if(dummy <= MIN_TIMEO){
										tmp->rto = MIN_TIMEO;
									}

#endif


									for(elem2 = tmp->timer_list_client; elem2 != NULL; elem2 = elem2->next){
										if(elem2->sockfd == tmp->sockfd){
											//print_list_tmr(tmp->timer_list_client);
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

							  					
							  					delete_sent_packet(elem2->sequence_number, tmp->sockfd, &tmp->list_sent_pkt);
							  					delete_id_timer(elem2->idTimer, tmp->sockfd, &tmp->timer_id_list_client);
							  					delete_timer(elem2->idTimer, tmp->sockfd, &tmp->timer_list_client);
							  					
							  					help_d_exp += 3;

							  					oper_window.sem_num = 0;
												oper_window.sem_op = 1;
												oper_window.sem_flg = 0;

resignal_l4:						  
												//wait sul valore del numero files
												if((semop(tmp->semid_window, &oper_window, 1)) == -1){
													perror("Error signaling 9\n");
													if(errno == EINTR){
														goto resignal_l4;
													}
													exit(-1);
												}

												
							  					tmp->ack_received++;
							  					//print_list_tmr(tmp->timer_list_client);

							  					if(flag_cycle == 1){
							  						flag_cycle = 0;
							  						break;
							  					}
							  					else{
							  						continue;
							  					}

											}

											if(tmp->ack_received == tmp->file_content + 2 || (strcmp(new_data->message,"404") == 0)){
												
												oper2.sem_num = 0;
										      	oper2.sem_op = -1;
										      	oper2.sem_flg = 0;

rewait_f48:
										      	//wait-->sono sicuro che expected non verrà sovrascritto perchè thread download è in join
										      	if((semop(tmp->semid_expected, &oper2, 1)) == -1){
										         	perror("Error signaling 19\n");
										         	if(errno == EINTR){
										         		goto rewait_f48;
										         	}
										         	exit(-1);
										      	}

												tmp->receiving_ack_phase_download = 0;
												tmp->ack_received = 0;
												free(new_data);
												tmp->retr_phase = 0;
												tmp->expected_next_seq_number += help_d_exp;
												tmp->sum_ack = 0;
												tmp->first_pkt_sent = 0;
												tmp->file_content = 0;
												
												oper_delete.sem_num = 0;
										      	oper_delete.sem_op = 1;
										      	oper_delete.sem_flg = 0;

resignal_f49:

										      	//signal dopo aver eventualmente eliminato timer multipli
										      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
										         	perror("Error waiting 14\n");
										         	if(errno == EINTR){
										         		goto resignal_f49;
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
					  				
									}

									if(tmp->ack_received == tmp->file_content + 2 || (strcmp(new_data->message,"404") == 0)){
										
										oper2.sem_num = 0;
								      	oper2.sem_op = -1;
								      	oper2.sem_flg = 0;

rewait_f50:
								      	//wait-->sono sicuro che expected non verrà sovrascritto perchè thread download è in join
								      	if((semop(tmp->semid_expected, &oper2, 1)) == -1){
								         	perror("Error signaling 19\n");
								         	if(errno == EINTR){
								         		goto rewait_f50;
								         	}
								         	exit(-1);
								      	}

										tmp->receiving_ack_phase_download = 0;
										tmp->ack_received = 0;
										free(new_data);
										tmp->retr_phase = 0;
										tmp->expected_next_seq_number += help_d_exp;
										tmp->sum_ack = 0;
										tmp->first_pkt_sent = 0;
										tmp->file_content = 0;
										
										oper_delete.sem_num = 0;
								      	oper_delete.sem_op = 1;
								      	oper_delete.sem_flg = 0;

resignal_f51:

								      	//signal dopo aver eventualmente eliminato timer multipli
								      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
								         	perror("Error waiting 15\n");
								         	if(errno == EINTR){
								         		goto resignal_f51;
								         	}
								         	exit(-1);
								      	}

								      	ret_val = 666;
										pthread_exit((void *)&ret_val);
									}
							    }
							}
						}

					}else{  //sto ricevendo duplicati
 						
						if(elem->sockfd == tmp->sockfd){
							if(last_ack == atoi(new_data->ack_number)){
								
								oper_sum.sem_num = 0;
						      	oper_sum.sem_op = -1;
						      	oper_sum.sem_flg = 0;

rewait_f52:
						      	//wait su semaforo per ack cumulativo
						      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
						         	perror("Error waiting 17\n");
						         	if(errno == EINTR){
						         		goto rewait_f52;
						         	}
						         	exit(-1);
						      	}

								tmp->sum_ack++;
								help_d_exp += 3;

								// handler tre pacchetti duplicati
								if(tmp->sum_ack == 3){
									// retransmit last ack correctly sent
									three_duplicate_message(new_data->ack_number, tmp->sockfd);
									tmp->sum_ack = 0; 
								}

								oper_sum.sem_num = 0;
						      	oper_sum.sem_op = 1;
						      	oper_sum.sem_flg = 0;

resignal_f53:
						      	//signal su semaforo per ack cumulativo
						      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
						         	perror("Error waiting 17\n");
						         	if(errno == EINTR){
						         		goto resignal_f53;
						         	}
						         	exit(-1);
						      	}
							}
						}

						break;
					}	
			
			}else{
				
				if(elem->sockfd == tmp->sockfd){
					//probabilmente ack duplicato
					if(last_ack == atoi(new_data->ack_number)){
						
						oper_sum.sem_num = 0;
				      	oper_sum.sem_op = -1;
				      	oper_sum.sem_flg = 0;

rewait_f54:
				      	//wait su semaforo per ack cumulativo
				      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
				         	perror("Error waiting 17\n");
				         	if(errno == EINTR){
				         		goto rewait_f54;
				         	}
				         	exit(-1);
				      	}

						tmp->sum_ack++;
						help_d_exp += 3;

						// handler tre pacchetti duplicati
						if(tmp->sum_ack == 3){
							// retransmit last ack correctly sent
							three_duplicate_message(new_data->ack_number, tmp->sockfd);
							tmp->sum_ack = 0; 
						}

						oper_sum.sem_num = 0;
				      	oper_sum.sem_op = 1;
				      	oper_sum.sem_flg = 0;

resignal_f55:
				      	//signal su semaforo per ack cumulativo
				      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
				         	perror("Error waiting 17\n");
				         	if(errno == EINTR){
				         		goto resignal_f55;
				         	}
				         	exit(-1);
				      	}

				      	break;
				    }
				}
			}
		}

		oper_delete.sem_num = 0;
      	oper_delete.sem_op = 1;
      	oper_delete.sem_flg = 0;

resignal_f56:

      	//wait-->sono sicuro che expected non verrà sovrascritto perchè thread download è in join
      	if((semop(tmp->semid_timer, &oper_delete, 1)) == -1){
         	perror("Error signaling 19\n");
         	if(errno == EINTR){
         		goto resignal_f56;
         	}
         	exit(-1);
      	}
	}		
} 


// thread che si occupa di servire una richiesta di download e di inviare la lista dei files disponibili, e di eseguire l'invio del file in caso sia presente
// nella directory del server
void *download(void *s_sock){
	
	int send_sock = *((int *)&s_sock);
	pthread_t tid_list_files_download;
	pthread_t tid_ack_download_handler;
	void *ret_val, *ret_val2;
	int ret_val1, port;
	int last_ack;
	int len1;
	float loss_p;
	t_client *tmp;
	struct sockaddr_in cliaddr1;
	int expected_next_seq_num;
	int ret;
	struct dirent **namelist;
	char path[MAXLINE];
	int n;
	int act_seq_num;
	char *tmp_seq;
	int  tmp_pkt;
	long int dimen;
	char *tmp_acknum;
	char *new_port;
	char *size_file;
	long int size;
	timer_t ret_timer;
	FILE *file;
	struct sembuf oper,oper2, oper_filename_before,oper_timer,oper_sum;
	struct sembuf oper_window;
	int sockfd_filename_download, sockfd_ack_download;
	struct sockaddr_in sin3,myNewAddrAckDownload;
	time_t begin;


	for (tmp=list_head; tmp!=NULL; tmp=tmp->next){
	 	if(tmp->sockfd == send_sock){
	 	 // verifico che la porta del client sia corretta
		 //	send_port = ntohs(cliaddr.sin_port);
			cliaddr1.sin_port = htons(tmp->port);
			inet_aton(tmp->ipaddr, &cliaddr1.sin_addr);
			cliaddr1.sin_family = AF_INET;
			tmp_pkt = tmp->sequence_number;
			if(tmp->expected_next_seq_number != 0){
				expected_next_seq_num = tmp->expected_next_seq_number;
			}
			tmp->download_list_phase = 1;
			last_ack = tmp->last_ack;
			tmp->operation_value = 2;

			break;
	 	}	
	}

	// parte il thread di list per fornire al client i files tra cui scegliere quello da scaricare
	ret = pthread_create(&tid_list_files_download, NULL, list_files, *(void **)&send_sock);
	if(ret != 0){
	  perror("pthread_create error");
	  exit(1);
	}

	ret_val = malloc(sizeof(int));
	if(ret_val == NULL){
		perror("\nmalloc error");
		exit(-1);
	}

	// attendo terminazione dell'operazione di listing dei files per far partire il vero e proprio downlaod
	pthread_join(tid_list_files_download, &ret_val);

	oper_timer.sem_num = 0;
   	oper_timer.sem_op = -1;
  	oper_timer.sem_flg = 0;

rewait_f57:

  	//wait sul valore timer
  	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
     	perror("Error waiting su timer di controllo\n");
     	if(errno == EINTR){
     		goto rewait_f57;
     	}
     	exit(-1);
  	}

  	////////////////////////////////////////////////////////////////////////////////
	tmp->master_timer = 1;
	ret_timer = srtScheduleS(tmp->sockfd, tmp->timerid);
	tmp->timerid++;

	//append non dovrebbe servire

	tmp->master_timer = 0;
	tmp->master_exists_flag = 1;
	
	oper_timer.sem_num = 0;
   	oper_timer.sem_op = 1;
  	oper_timer.sem_flg = 0;

resignal_f58:

  	//wait sul valore timer
  	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
     	perror("Error signaling su timer di controllo\n");
     	if(errno == EINTR){
     		goto resignal_f58;
     	}
     	exit(-1);
  	}

	sockfd_filename_download = tmp->sock_filename_download;

    printf("\n -------> Starting download!");
	fflush(stdout);
	
	t_data *new_data_file = malloc(sizeof(t_data));
	if(new_data_file == NULL){
		perror("\nmalloc error");
		exit(-1);
	}

rerecvfrom2:

	len1 = sizeof(cliaddr1);
	if(n = (recvfrom(sockfd_filename_download, new_data_file, 1500, 0, (struct sockaddr*)&cliaddr1, (socklen_t*)&len1)) == -1){
		perror("\n recvfrom error 2 PART DOWNLOAD");
		if(errno == EINTR){
			goto rerecvfrom2;
		}
		exit(-1);
		
	}

	close(sockfd_filename_download);
	tmp->sock_filename_download = 0;
	system("cd FILES_SERVER/");
	n = scandir("FILES_SERVER/", &namelist, file_select, alphasort);
	if (n == -1) {
		perror("scandir");
		exit(EXIT_FAILURE);
	}


	while (n--) {
		if(strncmp(new_data_file->message, namelist[n]->d_name, 256) == 0){
			break;
		}
	}

	strcpy(path, "FILES_SERVER/");
	strcat(path, new_data_file->message);
	
	sockfd_ack_download = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd_ack_download < 0){
        perror("\nsocket error");
        exit(-1);
    }

    memset((void *)&myNewAddrAckDownload, 0, sizeof(myNewAddrAckDownload));
    myNewAddrAckDownload.sin_family = AF_INET; 
    myNewAddrAckDownload.sin_addr.s_addr = htonl(INADDR_ANY); 
    myNewAddrAckDownload.sin_port = htons(0); 
    if(bind(sockfd_ack_download, (struct sockaddr*)&myNewAddrAckDownload, sizeof(myNewAddrAckDownload)) < 0){
    	perror("\n bind error on binding for thread ack list\n");
    	exit(-1);
    }

	tmp->sock_download = sockfd_ack_download;
    socklen_t len5 = sizeof(sin3);

    if (getsockname(sockfd_ack_download, (struct sockaddr *)&sin3, &len5) != 0)
	    perror("Error on getsockname");
	  	
	port = ntohs(sin3.sin_port);

	ret = pthread_create(&tid_ack_download_handler, NULL, ack_download_handler, (void *)tmp);
	if(ret != 0){
 		perror("pthread_create error");
  		exit(1);
	}

	tmp_pkt = tmp->sequence_number;

	// se file == NULL ---> allora il file richiesto dal client non esiste --> invio messaggio di errore
	if((file = fopen(path, "r")) == NULL){
		perror("fopen error");

		t_data *new_data_send = malloc(sizeof(t_data));
		if(new_data_send == NULL){
			perror("malloc error");
			exit(1);
		}
		// popolo sequence_number del packet di risposta
		asprintf(&tmp_seq,"%d",tmp_pkt);
		strcpy(new_data_send->sequence_number,tmp_seq);

		// popolo l'operation_no
		strcpy(new_data_send->operation_no,"2\0");

		// popolo ack_number del packet di risposta
		// popola act_seq_num per sapere il valore del ack_number per la risposta
		act_seq_num = atoi(new_data_file->sequence_number) + (int)strlen(new_data_file->message) + (int)strlen(new_data_file->operation_no);
		
		asprintf(&tmp_acknum, "%d", act_seq_num);
		strcpy(new_data_send->ack_number, tmp_acknum);

		asprintf(&new_port, "%d", port);
		strcpy(new_data_send->port, new_port);

		//file non presente nel server --> mando stringa di default al client
		strcpy(new_data_send->message, "404");

		tmp_pkt = tmp_pkt + (int)strlen(new_data_send->message) + (int)strlen(new_data_send->operation_no);
		tmp->sequence_number = tmp_pkt;
		tmp->ack_number = act_seq_num;

		oper_timer.sem_num = 0;
      	oper_timer.sem_op = -1;
      	oper_timer.sem_flg = 0;

rewait_f59:

      	//wait sui timer
      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error signaling 30\n");
         	if(errno == EINTR){
	     		goto rewait_f59;
	     	}
         	exit(-1);
      	}

      	oper_sum.sem_num = 0;
      	oper_sum.sem_op = -1;
      	oper_sum.sem_flg = 0;

rewait_f60:

      	//wait sui timer
      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
         	perror("Error signaling 31\n");
         	if(errno == EINTR){
	     		goto rewait_f60;
	     	}
         	exit(-1);
      	}

      	ret_timer = srtSchedule(tmp->sockfd,tmp->timerid,new_data_send->sequence_number,new_data_send->SYNbit,new_data_send->ACKbit,new_data_send->ack_number,new_data_send->message,new_data_send->operation_no,new_data_send->FINbit, new_data_send->port,new_data_send->size);
		
#ifdef ADPTO
		append_timer_adaptive(ret_timer, new_data_send->sequence_number, &tmp->timer_list_client, new_data_send->SYNbit, new_data_send->ACKbit, new_data_send->ack_number,new_data_send->message,new_data_send->operation_no,new_data_send->FINbit,tmp->sockfd, new_data_send->port,new_data_send->size, tmp->rto, 0.0, 0.0, 0);
#else
		append_timer(ret_timer, new_data_send->sequence_number, &tmp->timer_list_client, new_data_send->SYNbit, new_data_send->ACKbit, new_data_send->ack_number,new_data_send->message,new_data_send->operation_no,new_data_send->FINbit,tmp->sockfd, new_data_send->port,new_data_send->size);		
#endif	
		
		append_sent_packets(new_data_send->sequence_number, new_data_send->SYNbit, new_data_send->ACKbit, new_data_send->ack_number,new_data_send->message,new_data_send->operation_no,new_data_send->FINbit,tmp->sockfd, new_data_send->port, &tmp->list_sent_pkt, new_data_send->size);
		
		loss_p = float_rand(0.0, 1.0);
		
    	if(loss_p > LOSS_PROBABILITY){
    		if((sendto(send_sock, new_data_send, 1500, 0, (struct sockaddr*)&cliaddr1, sizeof(cliaddr1))) < 0){
				perror("errore sendto\n");
				exit(1);
			}
    	}
		
		expected_next_seq_num = act_seq_num;
		tmp->expected_next_seq_number = expected_next_seq_num;
			
		tmp->timerid++;
		
		oper_timer.sem_num = 0;
      	oper_timer.sem_op = 1;
      	oper_timer.sem_flg = 0;

resignal_f61:

      	//signal
      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error signaling 32\n");
         	if(errno == EINTR){
	     		goto resignal_f61;
	     	}
         	exit(-1);
      	}

      	oper_sum.sem_num = 0;
      	oper_sum.sem_op = 1;
      	oper_sum.sem_flg = 0;

resignal_f62:
      	//signal
      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
         	perror("Error signaling 33\n");
         	if(errno == EINTR){
	     		goto resignal_f62;
	     	}
         	exit(-1);
      	}

		oper.sem_num = 0;
      	oper.sem_op = 1;
      	oper.sem_flg = 0;

resignal_f63:

      	//signal
      	if((semop(tmp->semid_client, &oper, 1)) == -1){
         	perror("Error signaling 20\n");
         	if(errno == EINTR){
	     		goto resignal_f63;
	     	}
         	exit(-1);
      	}

		// perché il client una volta ricevuto il pkt dovrà mandare un ack verso il server 
		expected_ack = expected_ack + (int)strlen(new_data_send->message) + (int)strlen(new_data_send->operation_no);
		last_ack = tmp->last_ack;
		free(new_data_send);
		//return;
		

	}else{
		// se il file richiesto è presente inizio ad inviare il suo contenuto

		// popolo sequence_number del packet di risposta
	
		t_data *new_data_send1 = malloc(sizeof(t_data));
		if(new_data_send1 == NULL){
			perror("malloc error");
			exit(1);
		}

		asprintf(&tmp_seq,"%d",tmp_pkt);
		strcpy(new_data_send1->sequence_number,tmp_seq);

		fseek(file, 0, SEEK_END);
		size = ftell(file);
		asprintf(&size_file, "%ld",size);
		fseek(file, 0, SEEK_SET);

		strcpy(new_data_send1->size, size_file);

		// popolo l'operation_no
		strcpy(new_data_send1->operation_no,"2\0");

		// popolo ack_number del packet di risposta
		// popola act_seq_num per sapere il valore del ack_number per la risposta
		act_seq_num = atoi(new_data_file->sequence_number) + (int)strlen(new_data_file->message) + (int)strlen(new_data_file->operation_no);
		
		asprintf(&tmp_acknum, "%d", act_seq_num);
		strcpy(new_data_send1->ack_number, tmp_acknum);

		asprintf(&new_port, "%d", port);
		strcpy(new_data_send1->port, new_port);

		strcpy(new_data_send1->message, new_data_file->message);
		tmp_pkt = tmp_pkt + (int)strlen(new_data_send1->message) + (int)strlen(new_data_send1->operation_no);
		tmp->sequence_number = tmp_pkt;
		tmp->ack_number = act_seq_num;

		oper_timer.sem_num = 0;
      	oper_timer.sem_op = -1;
      	oper_timer.sem_flg = 0;

rewait_f64:

      	//wait sui timer
      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error signaling 34\n");
         	if(errno == EINTR){
	     		goto rewait_f64;
	     	}
         	exit(-1);
      	}


      	oper_sum.sem_num = 0;
      	oper_sum.sem_op = -1;
      	oper_sum.sem_flg = 0;

rewait_f65:

      	//wait sui timer
      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
         	perror("Error signaling 35\n");
         	if(errno == EINTR){
	     		goto rewait_f65;
	     	}
         	exit(-1);
      	}
		
		free(new_data_file); // libero il new_data contente il nome del file da downloadare

        ret_timer = srtSchedule(tmp->sockfd,tmp->timerid,new_data_send1->sequence_number,new_data_send1->SYNbit,new_data_send1->ACKbit,new_data_send1->ack_number,new_data_send1->message,new_data_send1->operation_no,new_data_send1->FINbit, new_data_send1->port,new_data_send1->size);
        
#ifdef ADPTO
        append_timer_adaptive(ret_timer, new_data_send1->sequence_number, &tmp->timer_list_client, new_data_send1->SYNbit, new_data_send1->ACKbit, new_data_send1->ack_number,new_data_send1->message,new_data_send1->operation_no,new_data_send1->FINbit,tmp->sockfd, new_data_send1->port,new_data_send1->size, tmp->rto, 0.0, 0.0, 0);
#else
        append_timer(ret_timer, new_data_send1->sequence_number, &tmp->timer_list_client, new_data_send1->SYNbit, new_data_send1->ACKbit, new_data_send1->ack_number,new_data_send1->message,new_data_send1->operation_no,new_data_send1->FINbit,tmp->sockfd, new_data_send1->port,new_data_send1->size);        
#endif

        append_sent_packets(new_data_send1->sequence_number, new_data_send1->SYNbit, new_data_send1->ACKbit, new_data_send1->ack_number,new_data_send1->message,new_data_send1->operation_no,new_data_send1->FINbit,tmp->sockfd, new_data_send1->port, &tmp->list_sent_pkt,new_data_send1->size);
		
		loss_p = float_rand(0.0, 1.0);
		
		if(loss_p > LOSS_PROBABILITY){
			if((sendto(send_sock, new_data_send1, 1500, 0,(struct sockaddr*)&cliaddr1, sizeof(cliaddr1))) < 0){
				perror("Errore in sendto\n");
				exit(1);
			}
		}

		tmp->first_pkt_sent = 1;
		tmp->filename_bytes = 1;

		expected_next_seq_num = act_seq_num;
		tmp->expected_next_seq_number = expected_next_seq_num;

        tmp->timerid++;
        //print_list_tmr(tmp->timer_list_client);

        oper_timer.sem_num = 0;
      	oper_timer.sem_op = 1;
      	oper_timer.sem_flg = 0;

resignal_f66:

      	//signal
      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error signaling 36\n");
         	if(errno == EINTR){
	     		goto resignal_f66;
	     	}
         	exit(-1);
      	}

      	oper_sum.sem_num = 0;
      	oper_sum.sem_op = 1;
      	oper_sum.sem_flg = 0;

resignal_f67:

      	//signal
      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
         	perror("Error signaling 37\n");
         	if(errno == EINTR){
	     		goto resignal_f67;
	     	}
         	exit(-1);
      	}

        oper.sem_num = 0;
      	oper.sem_op = 1;
      	oper.sem_flg = 0;

re_signaling38:

      	//signal
      	if((semop(tmp->semid_client, &oper, 1)) == -1){
         	perror("Error signaling 38\n");

         	if(errno == EINTR){
         		goto re_signaling38;
         	}
         	exit(-1);
      	}
        
        // perché il client una volta ricevuto il pkt dovrà mandare un ack verso il server 
        expected_ack = expected_ack + (int)strlen(new_data_send1->message) + (int)strlen(new_data_send1->operation_no);


        free(new_data_send1);
		
		// wait per aspettare che sia sempre mandato per primo il nome del file
		// in questo modo il client può correttamente creare il file locale
		oper_filename_before.sem_num = 0;
		oper_filename_before.sem_op = -1;
		oper_filename_before.sem_flg = 0;
		  

re_waiting_filename:
 
		//wait sul valore del numero files
		if((semop(tmp->semid_fileno, &oper_filename_before, 1)) == -1){
			perror("Error waiitng filename\n");
			if(errno == EINTR){
				goto re_waiting_filename;
			}
			
			exit(-1);
		}

		tmp->filename_bytes = 0;

		//////////////////////////////////////////////////////////////////////////////
	
		begin = time(NULL);

		///////////////////////////////////////////////////////////////////////////////
		
		t_data *new_data_send_cont = malloc(sizeof(t_data));
		if(new_data_send_cont == NULL){
			perror("malloc error");
			exit(1);
		}

		while(!feof(file)){

			oper_window.sem_num = 0;
          	oper_window.sem_op = -1;
          	oper_window.sem_flg = 0;

rewait_win:

          	//wait sui timer
          	if((semop(tmp->semid_window, &oper_window, 1)) == -1){
             	perror("\n semop error (543)");
             	if(errno == EINTR){
             		goto rewait_win;
             	}

             	exit(-1);
          	}

			// popolo sequence_number del packet di risposta
			asprintf(&tmp_seq,"%d",tmp_pkt);
			strcpy(new_data_send_cont->sequence_number,tmp_seq);

			dimen = fread(new_data_send_cont->message, sizeof(new_data_send_cont->message), 1, file);
			
			// popolo l'operation_no
			strcpy(new_data_send_cont->operation_no,"2\0");

			// popolo ack_number del packet di risposta
			// popola act_seq_num per sapere il valore del ack_number per la risposta
			act_seq_num = atoi(new_data_file->sequence_number) + (int)strlen(new_data_file->message) + (int)strlen(new_data_file->operation_no);
			
			asprintf(&tmp_acknum, "%d", act_seq_num);
			strcpy(new_data_send_cont->ack_number, tmp_acknum);

			asprintf(&new_port, "%d", port);
			strcpy(new_data_send_cont->port, new_port);

			tmp_pkt = tmp_pkt + (int)strlen(new_data_send_cont->message) + (int)strlen(new_data_send_cont->operation_no);
			tmp->sequence_number = tmp_pkt;
			tmp->ack_number = act_seq_num;

			oper_timer.sem_num = 0;
          	oper_timer.sem_op = -1;
          	oper_timer.sem_flg = 0;

re_signaling45:

          	//wait sui timer
          	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
             	
             	if(errno == EINTR){
             		goto re_signaling45;
             	}

             	exit(-1);
          	}


          	oper_sum.sem_num = 0;
          	oper_sum.sem_op = -1;
          	oper_sum.sem_flg = 0;

rewait_f68:

          	//wait sui timer
          	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
             	perror("Error signaling 39\n");
             	if(errno == EINTR){
		     		goto rewait_f68;
		     	}
             	exit(-1);
          	}				

        	ret_timer = srtSchedule(tmp->sockfd,tmp->timerid, new_data_send_cont->sequence_number,new_data_send_cont->SYNbit,new_data_send_cont->ACKbit,new_data_send_cont->ack_number,new_data_send_cont->message,new_data_send_cont->operation_no,new_data_send_cont->FINbit, new_data_send_cont->port,new_data_send_cont->size);
        	
#ifdef ADPTO
        	append_timer_adaptive(ret_timer, new_data_send_cont->sequence_number, &tmp->timer_list_client, new_data_send_cont->SYNbit, new_data_send_cont->ACKbit, new_data_send_cont->ack_number,new_data_send_cont->message,new_data_send_cont->operation_no,new_data_send_cont->FINbit,tmp->sockfd, new_data_send_cont->port,new_data_send_cont->size, tmp->rto, 0.0, 0.0, 0);
#else
        	append_timer(ret_timer, new_data_send_cont->sequence_number, &tmp->timer_list_client, new_data_send_cont->SYNbit, new_data_send_cont->ACKbit, new_data_send_cont->ack_number,new_data_send_cont->message,new_data_send_cont->operation_no,new_data_send_cont->FINbit,tmp->sockfd, new_data_send_cont->port,new_data_send_cont->size);
#endif
        	//print_list_tmr(tmp->timer_list_client);

        	append_sent_packets(new_data_send_cont->sequence_number, new_data_send_cont->SYNbit, new_data_send_cont->ACKbit, new_data_send_cont->ack_number,new_data_send_cont->message,new_data_send_cont->operation_no,new_data_send_cont->FINbit,tmp->sockfd, new_data_send_cont->port, &tmp->list_sent_pkt,new_data_send_cont->size);
			
			loss_p = float_rand(0.0, 1.0);
		
        	if(loss_p > LOSS_PROBABILITY){
        		if((sendto(send_sock, new_data_send_cont, 1500, 0, (struct sockaddr*)&cliaddr1, sizeof(cliaddr1))) < 0){
					perror("Errore in sendto\n");
					exit(1);
				}
        	}
			
        	tmp->timerid++;
        	tmp->file_content++;

        	// perché il client una volta ricevuto il pkt dovrà mandare un ack verso il server 
        	expected_ack = expected_ack + (int)strlen(new_data_send_cont->message) + (int)strlen(new_data_send_cont->operation_no);

        	memset(new_data_send_cont->sequence_number, 0, sizeof(new_data_send_cont->sequence_number));
        	memset(new_data_send_cont->operation_no, 0, sizeof(new_data_send_cont->operation_no));
        	memset(new_data_send_cont->message, 0, sizeof(new_data_send_cont->message));
        	memset(new_data_send_cont->port, 0, sizeof(new_data_send_cont->port));

        	oper_timer.sem_num = 0;
          	oper_timer.sem_op = 1;
          	oper_timer.sem_flg = 0;

resignal_f69:

          	//signal
          	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
             	perror("Error signaling 40\n");
             	if(errno == EINTR){
		     		goto resignal_f69;
		     	}
             	exit(-1);
          	}

          	oper_sum.sem_num = 0;
          	oper_sum.sem_op = 1;
          	oper_sum.sem_flg = 0;

resignal_f70:
          	//signal
          	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
             	perror("Error signaling 41\n");
             	if(errno == EINTR){
		     		goto resignal_f70;
		     	}
             	exit(-1);
          	}

        	oper.sem_num = 0;
          	oper.sem_op = 1;
          	oper.sem_flg = 0;

resignal_f71:
          	//signal
          	if((semop(tmp->semid_client, &oper, 1)) == -1){
             	perror("Error signaling 22\n");
             	if(errno == EINTR){
		     		goto resignal_f71;
		     	}
             	exit(-1);
          	}
        	
		}

		free(new_data_send_cont);

		oper_window.sem_num = 0;
      	oper_window.sem_op = -1;
      	oper_window.sem_flg = 0;

rewait_win2:

      	//wait sui timer
      	if((semop(tmp->semid_window, &oper_window, 1)) == -1){
         	perror("\n semop error (678)");
         	if(errno == EINTR){
         		goto rewait_win2;
         	}

         	exit(-1);
      	}

		t_data *new_data_send2 = malloc(sizeof(t_data));
		if(new_data_send2 == NULL){
			perror("malloc error");
			exit(1);
		}
		// popolo sequence_number del packet di risposta
		asprintf(&tmp_seq,"%d",tmp_pkt);
		strcpy(new_data_send2->sequence_number,tmp_seq);

		// popolo l'operation_no
		strcpy(new_data_send2->operation_no,"2\0");

		// popolo ack_number del packet di risposta
		// popola act_seq_num per sapere il valore del ack_number per la risposta
		act_seq_num = atoi(new_data_file->sequence_number) + (int)strlen(new_data_file->message) + (int)strlen(new_data_file->operation_no);
		asprintf(&tmp_acknum, "%d", act_seq_num);
		strcpy(new_data_send2->ack_number, tmp_acknum);

		asprintf(&new_port, "%d", port);
		strcpy(new_data_send2->port, new_port);

		memset(new_data_send2->message, 0, MAXLINE);
		strcpy(new_data_send2->message, def_str2);

		tmp_pkt = tmp_pkt + (int)strlen(new_data_send2->message) + (int)strlen(new_data_send2->operation_no);
		tmp->sequence_number = tmp_pkt;
		tmp->ack_number = act_seq_num;

		oper_timer.sem_num = 0;
      	oper_timer.sem_op = -1;
      	oper_timer.sem_flg = 0;

re_signaling43:

      	//wait sui timer
      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	perror("Error signaling 43\n");
         	goto re_signaling43;
         	
      	}


      	oper_sum.sem_num = 0;
      	oper_sum.sem_op = -1;
      	oper_sum.sem_flg = 0;

rewait_f73:

      	//wait sui timer
      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
         	perror("Error signaling 44\n");
         	if(errno == EINTR){
         		goto rewait_f73;
         	}
         	exit(-1);
      	}				

      	ret_timer = srtSchedule(tmp->sockfd,tmp->timerid,new_data_send2->sequence_number,new_data_send2->SYNbit,new_data_send2->ACKbit,new_data_send2->ack_number,new_data_send2->message,new_data_send2->operation_no,new_data_send2->FINbit, new_data_send2->port,new_data_send2->size);
        
#ifdef ADPTO
        append_timer_adaptive(ret_timer, new_data_send2->sequence_number, &tmp->timer_list_client, new_data_send2->SYNbit, new_data_send2->ACKbit, new_data_send2->ack_number,new_data_send2->message,new_data_send2->operation_no,new_data_send2->FINbit,tmp->sockfd, new_data_send2->port,new_data_send2->size, tmp->rto, 0.0, 0.0, 0);
#else
        append_timer(ret_timer, new_data_send2->sequence_number, &tmp->timer_list_client, new_data_send2->SYNbit, new_data_send2->ACKbit, new_data_send2->ack_number,new_data_send2->message,new_data_send2->operation_no,new_data_send2->FINbit,tmp->sockfd, new_data_send2->port,new_data_send2->size);        
#endif
        //print_list_tmr(tmp->timer_list_client);

        append_sent_packets(new_data_send2->sequence_number, new_data_send2->SYNbit, new_data_send2->ACKbit, new_data_send2->ack_number,new_data_send2->message,new_data_send2->operation_no,new_data_send2->FINbit,tmp->sockfd, new_data_send2->port, &tmp->list_sent_pkt, new_data_send2->size);
		
		loss_p = float_rand(0.0, 1.0);
		
    	if(loss_p > LOSS_PROBABILITY){
    		if((sendto(send_sock, new_data_send2, 1500, 0,(struct sockaddr*)&cliaddr1, sizeof(cliaddr1))) < 0){
				perror("errore sendto\n");
				exit(1);
			}
    	}
		
        tmp->timerid++;
        
        oper_timer.sem_num = 0;
      	oper_timer.sem_op = 1;
      	oper_timer.sem_flg = 0;

resignal_f74:

      	//signal
      	if((semop(tmp->semid_timer, &oper_timer, 1)) == -1){
         	if(errno == EINTR){
         		goto resignal_f74;
         	}
         	exit(-1);
      	}

      	oper_sum.sem_num = 0;
      	oper_sum.sem_op = 1;
      	oper_sum.sem_flg = 0;

resignal_f75:

      	//signal
      	if((semop(tmp->semid_sum_ack, &oper_sum, 1)) == -1){
         	perror("Error signaling 46\n");
         	if(errno == EINTR){
         		goto resignal_f75;
         	}
         	exit(-1);
      	}

        oper.sem_num = 0;
      	oper.sem_op = 1;
      	oper.sem_flg = 0;

resignal_f76:
      	//signal
      	if((semop(tmp->semid_client, &oper, 1)) == -1){
         	perror("Error signaling 23\n");
         	if(errno == EINTR){
         		goto resignal_f76;
         	}
         	exit(-1);
      	}
        
        // perché il client una volta ricevuto il pkt dovrà mandare un ack verso il server 
        expected_ack = expected_ack + (int)strlen(new_data_send2->message) + (int)strlen(new_data_send2->operation_no);
        memset(new_data_send2, 0, sizeof(t_data));
		free(new_data_send2);
			
		
	}

	ret_val2 = malloc(sizeof(int));
	if(ret_val2 == NULL){
		perror("\nmalloc error");
		exit(-1);
	}

	oper2.sem_num = 0;
  	oper2.sem_op = 1;
  	oper2.sem_flg = 0;

resignal_f77:

  	//signal da vedere se serve effettivamente
  	if((semop(tmp->semid_expected, &oper2, 1)) == -1){
     	perror("Error signaling 24\n");
     	if(errno == EINTR){
     		goto resignal_f77;
     	}
     	exit(-1);
  	}

	pthread_join(tid_ack_download_handler, &ret_val2);
	
	tmp->download_list_phase = 0;

	if(tmp->master_exists_flag == 1){
		
		ret = timer_delete(tmp->master_IDTimer);	  				    
		if(ret == -1){
			perror("\ntimer_delete error");
			exit(-1);
		}
		tmp->master_exists_flag = 0;
	}

	printf("\n Task download completed... Bye!");
	fflush(stdout);

	//print_list_sent_pkt(tmp->list_sent_pkt);
	ret_val1 = 666;

	/////////////////////////////////////////////////////////////////////////////
	time_t end = time(NULL);

	printf("\n TIME: %ju sec", (uintmax_t)(end - begin));
	fflush(stdout);
	/////////////////////////////////////////////////////////////////////////////

	pthread_exit((void *)&ret_val1);
    
}


void operation_func(int send_sock, int choose, t_data *new_data){

	int ret;
	pthread_t tid;
	t_client *tmp;

	for(tmp = list_head; tmp != NULL; tmp = tmp->next){
		if(tmp->sockfd == send_sock){
			break;
		}
	}

	
	switch(choose){

		case 0:

			//gestione three way handshake nel main da spostare nel main
			break;
				
		case 1:

			new_data_gl_list = malloc(sizeof(t_data));
			if(new_data_gl_list == NULL){
				perror("\n malloc error");
				exit(-1);
			}

			strcpy(new_data_gl_list->sequence_number, new_data->sequence_number);
			strcpy(new_data_gl_list->operation_no, new_data->operation_no);
			memcpy(new_data_gl_list->message, new_data->message, MAXLINE);
			
			ret = pthread_create(&tid, NULL, list_files, *((void **)&send_sock));
			if(ret != 0){
				perror("pthread_create error");
				exit(1);
			}
					
			break;
               
        case 2:
        
          	new_data_gl_download = malloc(sizeof(t_data));
			if(new_data_gl_download == NULL){
				perror("\n malloc error");
				exit(-1);
			}

			strcpy(new_data_gl_download->sequence_number, new_data->sequence_number);
			strcpy(new_data_gl_download->operation_no, new_data->operation_no);
			memcpy(new_data_gl_download->message, new_data->message, MAXLINE);
          	//copiando il server controlla se il buffer era presente nel database
          	memset(buffer, 0, MAXLINE);
          	memcpy(buffer, new_data->message, MAXLINE);

          	ret = pthread_create(&tid, NULL, download, *((void **)&send_sock));
			if(ret != 0){
				perror("pthread_create error");
				exit(1);
			}
							
         	break;
                
       	case 3:
       		
        	new_data_gl_upload = malloc(sizeof(t_data));
			if(new_data_gl_upload == NULL){
				perror("\n malloc error");
				exit(-1);
			}

			strcpy(new_data_gl_upload->sequence_number, new_data->sequence_number);
			strcpy(new_data_gl_upload->operation_no, new_data->operation_no);
			memcpy(new_data_gl_upload->message, new_data->message, MAXLINE);
        	memset(buffer1, 0, MAXLINE);
          	memcpy(buffer1, new_data->message, MAXLINE);

          	if((strlen(buffer1) == 0) && (tmp->upload_tid == NULL)){

          		ret = pthread_create(&tid, NULL, upload, *((void **)&send_sock));
				if(ret != 0){
					perror("pthread_create error");
					exit(1);
				}
          	}


		
        	break;
        
        case 4:

        	break;

        default:
	      	printf("WARNING: invalid choice.\nReinsert the option\n");
          	fflush(stdout);
	}

}


int main() 
{ 
	 
	ssize_t 	  n; 
	socklen_t 	  len;  
	struct        sigaction act; 
	sigset_t      set;
	int 		  choose;
	t_client 	  *tmp, *tmp2,*tmp3;
	int 		  send_sock, ret;
	char 		  random[10];
	int 		  val_seq;
	pthread_t 	  tid_rt;
	struct sembuf oper_control;
	struct sembuf oper_timer;
	t_tmr*        timer;
	float         loss_p;
	timer_t       ret_timer;

	sigset_t 	  block;
	
	sigemptyset(&block);
	sigaddset(&block, 34);
	pthread_sigmask(SIG_BLOCK, &block, NULL);

	pthread_create(&tid_rt, NULL, signal_handler_thread, NULL);

	memset((void *)&servaddr, 0, sizeof(servaddr));
	servaddr.sin_family = AF_INET; 
	servaddr.sin_addr.s_addr = htonl(INADDR_ANY); 
	servaddr.sin_port = htons(PORT); 


	
	sigfillset(&set);
	act.sa_mask = set;
	act.sa_sigaction = handler;
	act.sa_flags = 0;
	sigaction(SIGINT, &act, NULL);

	printf("\n Server ON...\n");
    fflush(stdout);

	/* create UDP socket */
	udpfd = socket(AF_INET, SOCK_DGRAM, 0);
	if (udpfd < 0) { 
    	perror("errore in socket");
    	exit(1);
  	}

	// binding server addr structure to udp sockfd 
	if(bind(udpfd, (struct sockaddr*)&servaddr, sizeof(servaddr)) < 0){
		perror("\n bind error");
		exit(-1);
	}

	semid_server_closure = semget(IPC_PRIVATE, 1 , IPC_CREAT|IPC_EXCL|0666);
	if(semid_server_closure == -1){
		perror("\nsemget error");
		exit(-1);
	}

	if(semctl(semid_server_closure, 0, SETVAL, 1) == -1){
    	perror("Unable to set the sem to zero value\n");
    	semctl(semid_server_closure, -1, IPC_RMID, NULL);
    	exit(-1);
  	}
	
	srand(time(NULL));
	while(1) { 
		
rerecv:
		
		len = sizeof(cliaddr);
		// in case of first packet three way handshake

		t_data *new_data = malloc(sizeof(t_data));
		if(new_data == 0){
			perror("malloc error");
			exit(1);
		} 

		n = recvfrom(udpfd, new_data, 1500, 0, (struct sockaddr*)&cliaddr, (socklen_t*)&len);
		if(n < 0){
			free(new_data);
			errno = 0;
			goto rerecv;
		}

		
		if(n == 0){
			if(close(udpfd) == -1){
				perror("errore in close");
				exit(1);
			}
		}

		if(errno == EINTR){
			errno = 0;
			goto rerecv;	
		}


		for(tmp3 = list_head; tmp3 != NULL; tmp3=tmp3->next){
			
			
			if(tmp3->port == ntohs(cliaddr.sin_port)){
				
				if(tmp3->hs == 1){
					break;
				}

				if((strcmp(new_data->FINbit,"1")) ==0){
					break;
				}

				if((strcmp(new_data->ACKbit,"1")) ==0){
					break;
				}

		 		// verifico che la porta del client sia corretta
		 		oper_control.sem_num = 0;
				oper_control.sem_op = -1;
				oper_control.sem_flg = 0;
				  	

resignaling25:  
				
				//wait su lista timer
				if((semop(tmp3->semid_timer, &oper_control, 1)) == -1){
					perror("Error signaling 25\n");
					if(errno == EINTR){
						goto resignaling25;
					}
					exit(-1);
				}

				print_list_tmr(tmp3->timer_list_client);
				
				if(tmp3->timer_list_client != NULL){
					delete_id_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_id_list_client);
					delete_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_list_client);
				}
				
	
				for(timer = tmp3->timer_list_client; timer!=NULL; timer=timer->next){
					
					if(timer->sockfd == tmp3->sockfd){

						free(new_data);

						oper_control.sem_num = 0;
						oper_control.sem_op = 1;
						oper_control.sem_flg = 0;

resignal_f78:					  	  
						//signal su lista timer
						if((semop(tmp3->semid_timer, &oper_control, 1)) == -1){
							perror("Error signaling 27\n");
							if(errno == EINTR){
				         		goto resignal_f78;
				         	}
							exit(-1);
						}

						goto rerecv;
					}
				}

				oper_control.sem_num = 0;
				oper_control.sem_op = 1;
				oper_control.sem_flg = 0;

resignal_f79:				  	  
				//signal su lista timer
				if((semop(tmp3->semid_timer, &oper_control, 1)) == -1){
					perror("Error signaling 28\n");
					if(errno == EINTR){
		         		goto resignal_f79;
		         	}
					exit(-1);
				}

				break;
			}
				
		}	

		//sto chiudendo lato server venendo dall'handler
		if(con_close == 1){
			
			//devo fare controllo ip e porta scorrendo la lista di tutti clients
			//se lo trovo,faccio qua sotto e controllo se mi ha ackato bene dove num=client.random number
			for(tmp2=list_head;tmp2!=NULL;tmp2=tmp2->next){

				if(tmp2->port == ntohs(cliaddr.sin_port) && (strcmp(tmp2->ipaddr, inet_ntoa(cliaddr.sin_addr)) == 0)){
					
					num = tmp2->rand_c;
					snprintf(random, 10, "%d", num +1);
					
					if((strncmp(new_data->ACKbit, "1", 2) == 0) && (strncmp(new_data->ack_number, random, sizeof(new_data->ack_number))== 0)){

						oper_timer.sem_num = 0;
						oper_timer.sem_op = -1;
						oper_timer.sem_flg = 0;

rewait_fin_wait2:

						//signal su lista timer
						if((semop(tmp2->semid_timer, &oper_timer, 1)) == -1){
							perror("Error waiting fin_wait2\n");
							if(errno == EINTR){
								goto rewait_fin_wait2;
							}
							exit(-1);
						}

						ret = timer_delete((tmp2->timer_list_client)->idTimer);
						if(ret == -1){
							perror("\n timer_delete error");
							exit(-1);
						}
						delete_id_timer((tmp2->timer_list_client)->idTimer,tmp2->sockfd,&tmp3->timer_id_list_client);
						delete_timer((tmp2->timer_list_client)->idTimer,tmp2->sockfd,&tmp2->timer_list_client);

						oper_timer.sem_num = 0;
						oper_timer.sem_op = 1;
						oper_timer.sem_flg = 0;

resignal_fin_wait2:

						//signal su lista timer
						if((semop(tmp2->semid_timer, &oper_timer, 1)) == -1){
							perror("Error waiting fin_wait2\n");
							if(errno == EINTR){
								goto resignal_fin_wait2;
							}
							exit(-1);
						}

						if(final_counter > 0){
							final_counter--;
							free(new_data);
							goto rerecv; //mi aspetto secondo pkt
			    		}else{
			    			goto rerecv;
			    		}
					}
				}		
			}	
		}
		
		//gestione chiusura da parte del client
		if((strncmp(new_data->FINbit,"1",sizeof(new_data->FINbit))==0) && con_close == 0){
			if(list_head == NULL || tmp3 == NULL){
				goto rerecv;
			}

			tmp3->flag_close = 1;
			snprintf(random, 10, "%d", tmp3->rand_hs+1);
		
			if((strncmp(new_data->ack_number, random, sizeof(new_data->ack_number)) == 0) && (strncmp(new_data->ACKbit, "1", 2) == 0)){
				if((tmp3->timer_list_client)->idTimer != NULL){
					ret = timer_delete((tmp3->timer_list_client)->idTimer);
					delete_id_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_id_list_client);
					delete_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_list_client);
				}
				
				delete(ntohs(cliaddr.sin_port), inet_ntoa(cliaddr.sin_addr), &list_head, &list_head_d);
				//print_list(list_head);
            	//print_list_des(list_head_d);
				continue;
			}

            t_data * new_data_fin = malloc(sizeof(t_data));
            if(new_data_fin == NULL){
            	perror("\nmalloc error");
            	exit(1);
            }

            val_seq = atoi(new_data->sequence_number);
            snprintf(new_data_fin->ack_number, sizeof(new_data_fin->ack_number), "%d",  val_seq + 1);
            strcpy(new_data_fin->ACKbit, "1\0");

            loss_p = float_rand(0.0,1.0);
            
            if(loss_p >= LOSS_PROBABILITY){
            	if(sendto(udpfd, new_data_fin, 1500, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) < 0){
	                perror("(1)errore in sendto");
	                exit(1);
	            }
            }

            oper_control.sem_num = 0;
			oper_control.sem_op = -1;
			oper_control.sem_flg = 0;

rewait_f80:

			//signal su lista timer
			if((semop(tmp3->semid_timer, &oper_control, 1)) == -1){
				perror("Error signaling 28\n");
				if(errno == EINTR){
					goto rewait_f80;
				}
				exit(-1);
			}

			for(timer = tmp3->timer_list_client; timer!=NULL; timer=timer->next){
				if(strcmp(timer->ACKbit,"1") == 0){
					ret = timer_delete((tmp3->timer_list_client)->idTimer);
					delete_id_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_id_list_client);
					delete_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_list_client);
				}
			}

			tmp3->flag_last_ack_sent = 1;
            ret_timer = srtSchedule(tmp3->sockfd,tmp3->timerid,new_data_fin->sequence_number,new_data_fin->SYNbit,new_data_fin->ACKbit,new_data_fin->ack_number,new_data_fin->message,new_data_fin->operation_no,new_data_fin->FINbit, new_data_fin->port,new_data_fin->size);
			
#ifdef ADPTO
			append_timer_adaptive(ret_timer, new_data_fin->sequence_number, &tmp3->timer_list_client, new_data_fin->SYNbit, new_data_fin->ACKbit, new_data_fin->ack_number,new_data_fin->message,new_data_fin->operation_no,new_data_fin->FINbit,tmp3->sockfd, new_data_fin->port,new_data_fin->size, tmp3->rto, 0.0, 0.0, 0);
#else
			append_timer(ret_timer, new_data_fin->sequence_number, &tmp3->timer_list_client, new_data_fin->SYNbit, new_data_fin->ACKbit, new_data_fin->ack_number,new_data_fin->message,new_data_fin->operation_no,new_data_fin->FINbit,tmp3->sockfd, new_data_fin->port,new_data_fin->size);
#endif			
			tmp3->timerid++;

			oper_control.sem_num = 0;
			oper_control.sem_op = 1;
			oper_control.sem_flg = 0;

resignal_f81:

			//signal su lista timer
			if((semop(tmp3->semid_timer, &oper_control, 1)) == -1){
				perror("Error signaling 28\n");
				if(errno == EINTR){
					goto resignal_f81;
				}
				exit(-1);
			}
            
            free(new_data_fin);
            free(new_data);

            goto rerecv;

		}else if((strcmp(new_data->FINbit, "1") == 0) && con_close == 1){

			//ciclo,confronto ip e porta e se trovato decremento e mando ultimo messaggio al singolo client
			//che deve gestire il fatto che sia ultimo pkt del server e quindi si chiuderà pure il client
			//quando entrambi i counter a zero, aspetto i 4 minuti e chiudo

			t_data *new_data2 = malloc(sizeof(t_data));
			if(new_data2 == NULL){
				perror("\nmalloc error");
				exit(1);
			}

			strcpy(new_data2->ACKbit, "1\0");
			val_seq=atoi(new_data->sequence_number);
			snprintf(new_data2->ack_number, sizeof(new_data2->ack_number), "%d", val_seq+1);
			
			//lui mi ha riscritto il primo messaggio di hs, rimando un nuovo pkt eliminando quello vecchio
			oper_timer.sem_num = 0;
			oper_timer.sem_op = -1;
			oper_timer.sem_flg = 0;

rewait_timed_wait:

			//signal su lista timer
			if((semop(tmp3->semid_timer, &oper_timer, 1)) == -1){
				perror("Error waiting timed_wait\n");
				if(errno == EINTR){
					goto rewait_timed_wait;
				}
				exit(-1);
			}

			loss_p = float_rand(0.0, 1.0);
		
			if(loss_p >= LOSS_PROBABILITY){
				if(sendto(udpfd, new_data2, 1500, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) < 0){
					perror("\nsendto error");
					exit(1);
				}
			}

			if(tmp3->timer_list_client != NULL){
				ret = timer_delete((tmp3->timer_list_client)->idTimer);
				delete_id_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_id_list_client);
				delete_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_list_client);
			}

			tmp3->flag_last_ack_sent = 1;
		
			ret_timer = srtSchedule(tmp3->sockfd,tmp3->timerid,new_data2->sequence_number,new_data2->SYNbit,new_data2->ACKbit,new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit, new_data2->port,new_data2->size);
			
#ifdef ADPTO
			append_timer_adaptive(ret_timer, new_data2->sequence_number, &tmp3->timer_list_client, new_data2->SYNbit, new_data2->ACKbit, new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit,tmp3->sockfd, new_data2->port, new_data2->size, tmp3->rto, 0.0, 0.0, 0);
#else
			append_timer(ret_timer, new_data2->sequence_number, &tmp3->timer_list_client, new_data2->SYNbit, new_data2->ACKbit, new_data2->ack_number,new_data2->message,new_data2->operation_no,new_data2->FINbit,tmp3->sockfd, new_data2->port, new_data2->size);
#endif
			tmp3->timerid++;
			
			//lui mi ha riscritto il primo messaggio di hs, rimando un nuovo pkt eliminando quello vecchio
			oper_timer.sem_num = 0;
			oper_timer.sem_op = 1;
			oper_timer.sem_flg = 0;

resignal_timed_wait:

			//signal su lista timer
			if((semop(tmp3->semid_timer, &oper_timer, 1)) == -1){
				perror("Error signaling timed_wait\n");
				if(errno == EINTR){
					goto resignal_timed_wait;
				}
				exit(-1);
			}

			free(new_data2);
			tmp3->flag_server_close = 1;
				
			goto rerecv; //devo aspettare tutti
		
		}

		if(strcmp(new_data->operation_no, "") != 0){
			printf("\n Client (%hu - %s) has requested op_no: %s\n", ntohs(cliaddr.sin_port), inet_ntoa(cliaddr.sin_addr), new_data->operation_no); 
		}
		
		choose = atoi(new_data->operation_no);
		
	
		if(choose == 0){
			
			
			for (tmp3 = list_head; tmp3 != NULL; tmp3 = tmp3->next){
			 	if(tmp3->port == ntohs(cliaddr.sin_port)){
			 	 // verifico che la porta del client sia corretta
					break;
			 	}	
			}

			if(tmp3 == NULL){
				num = rand()%10000;
				append_queue(ntohs(cliaddr.sin_port), inet_ntoa(cliaddr.sin_addr), num, &list_head, &list_head_d);
			}

			for (tmp3=list_head; tmp3!=NULL; tmp3=tmp3->next){
			 	if(tmp3->port == ntohs(cliaddr.sin_port)){
			 	 // verifico che la porta del client sia corretta
					break;
			 	}	
			}

			int var = (tmp3->rand_hs) +1;
			snprintf(random, 10, "%d", var);
		
			if(strcmp(new_data->ack_number, random) == 0){
				
				if(tmp3->estab_s == 0){
					
					ret = timer_delete((tmp3->timer_list_client)->idTimer);
					delete_id_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_id_list_client);
					delete_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_list_client);
					tmp3->hs = 0;
	            	tmp3->estab_s = 1;
	          		free(new_data);
	            	//print_list(list_head);
					//print_list_des(list_head_d);
					printf("\n*******ESTAB Server SOCKFD %d********\n", tmp3->sockfd);
	            	fflush(stdout);
					errno = 0;
	            	continue;
				}else{
					continue;
				}
				
            }

			t_data *new_data_hs = malloc(sizeof(t_data));
            if(new_data_hs == NULL){
                perror("malloc error");
                exit(1);
            }

			//lui mi ha riscritto il primo messaggio di hs, rimando un nuovo pkt eliminando quello vecchio
			oper_control.sem_num = 0;
			oper_control.sem_op = -1;
			oper_control.sem_flg = 0;

rewaitx:

			//signal su lista timer
			if((semop(tmp3->semid_timer, &oper_control, 1)) == -1){
				perror("Error waiting x\n");
				if(errno == EINTR){
					goto rewaitx;
				}
				exit(-1);
			}

			if(tmp3->timer_list_client != NULL){
				ret = timer_delete((tmp3->timer_list_client)->idTimer);
				delete_id_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_id_list_client);
				delete_timer((tmp3->timer_list_client)->idTimer,tmp3->sockfd,&tmp3->timer_list_client);
			}

			
            strcpy(new_data_hs->SYNbit, "1\0");
            strcpy(new_data_hs->ACKbit, "1\0");
            //srand(time(NULL));
            val_seq = atoi(new_data->sequence_number);
            snprintf(new_data_hs->sequence_number, sizeof(new_data_hs->sequence_number), "%d", tmp3->rand_hs);
            snprintf(new_data_hs->ack_number, sizeof(new_data_hs->ack_number), "%d", val_seq +1);
			
			loss_p = float_rand(0.0,1.0);
			
			if(loss_p >= LOSS_PROBABILITY){
				if(sendto(udpfd, new_data_hs, 1500, 0, (struct sockaddr *)&cliaddr, sizeof(cliaddr)) < 0){
	                perror("(3)errore in sendto");
	                exit(1);
	            }
			}

			ret_timer = srtSchedule(tmp3->sockfd,tmp3->timerid,new_data_hs->sequence_number,new_data_hs->SYNbit,new_data_hs->ACKbit,new_data_hs->ack_number,new_data_hs->message,new_data_hs->operation_no,new_data_hs->FINbit, new_data_hs->port,new_data_hs->size);
			
#ifdef ADPTO
			append_timer_adaptive(ret_timer, new_data_hs->sequence_number, &tmp3->timer_list_client, new_data_hs->SYNbit, new_data_hs->ACKbit, new_data_hs->ack_number,new_data_hs->message,new_data_hs->operation_no,new_data_hs->FINbit,tmp3->sockfd, new_data_hs->port,new_data_hs->size, tmp3->rto, 0.0, 0.0, 0);
#else
			append_timer(ret_timer, new_data_hs->sequence_number, &tmp3->timer_list_client, new_data_hs->SYNbit, new_data_hs->ACKbit, new_data_hs->ack_number,new_data_hs->message,new_data_hs->operation_no,new_data_hs->FINbit,tmp3->sockfd, new_data_hs->port, new_data_hs->size);			
#endif
			tmp3->timerid++;
			
			oper_control.sem_num = 0;
			oper_control.sem_op = 1;
			oper_control.sem_flg = 0;

rewaitxx:

			//signal su lista timer
			if((semop(tmp3->semid_timer, &oper_control, 1)) == -1){
				perror("Error rewaitxx\n");
				if(errno == EINTR){
					goto rewaitxx;
				}
				exit(-1);
			}
			
            free(new_data_hs);
            free(new_data);
            

		}else{

			for (tmp=list_head; tmp!=NULL; tmp=tmp->next){
	 	 		if(tmp->port == ntohs(cliaddr.sin_port) && (strcmp(tmp->ipaddr, inet_ntoa(cliaddr.sin_addr)) == 0)){
	 	 			send_sock = tmp->sockfd;
	 	 		}	
			}

			operation_func(send_sock, choose, new_data);
		}

		print_list(list_head);
		print_list_des(list_head_d);

		
	}	
	
} 
